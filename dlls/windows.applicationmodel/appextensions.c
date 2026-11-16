/* WinRT Windows.ApplicationModel.AppExtensions.AppExtensionCatalog implementation
 *
 * Copyright (C) 2026 Wine Project Contributors
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
 * Unpackaged winget (v1.29) resolves its pre-indexed "winget" source through
 * AppExtensionCatalog::Open("com.microsoft.winget.source"): it activates the
 * catalog runtime class, awaits the IAsyncOperation<IVectorView<AppExtension*>>
 * and looks for an extension whose Id is "IndexDB" whose Package name matches
 * the source reference name.  A matching catalog entry lets the source open the
 * pre-indexed sqlite database from the package's InstalledLocation
 * (Public\index.db) without any AddPackageAsync/msix staging.
 *
 * The catalog answers with a single hand-built source package:
 *
 *   Microsoft.Winget.Source / Microsoft.Winget.Source_8wekyb3d8bbwe
 *   InstalledLocation -> AppModel\Packages\<PFN>\PackagePath (registry seed)
 *
 * The InstalledLocation objects are deliberately laid out so that the same
 * pointer serves as both a COM object (vtable at +0, get_Path at slot 7) and
 * as a WinRT HSTRING header ({flags,len} + inline chars at +8): the projection
 * in WindowsPackageManager.dll reads the package name straight out of the
 * pointer returned by IPackage::get_InstalledLocation (slots observed in the
 * v1.29 binary: Package[9], InstalledLocation[7], PackageId[6]/[7]).  That
 * requires the vtable pointer high dword to equal the string length, so the
 * vtable is a heap block mmap'd at (len << 32).
 */

#include "private.h"
#include "objbase.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appextensions);

#define heap_alloc_zero(n) HeapAlloc( GetProcessHeap(), 0, n )
#define heap_free(p) HeapFree( GetProcessHeap(), 0, p )

static const WCHAR source_package_name[] = L"Microsoft.Winget.Source";
static const WCHAR source_package_full_name[] = L"Microsoft.Winget.Source_8wekyb3d8bbwe";
static const WCHAR source_publisher_id[] = L"8wekyb3d8bbwe";
static const WCHAR source_extension_id[] = L"IndexDB";

/* Version reported for the source package; kept in sync with the Version
 * stored in user_sources.json so the update flow considers the source fresh. */
struct version_abi
{
    UINT16 revision;
    UINT16 major;
    UINT16 minor;
    UINT16 build;
};
static const struct version_abi source_version = { 0, 2, 0, 0 }; /* 2.0.0.0 */

/* Installed location of the source package (directory containing Public\index.db
 * and Index\<version>\Main\index.db).  Registry seed first, like the rest of the
 * AppModel identity plumbing. */
static const WCHAR *source_package_path( void )
{
    static WCHAR path[MAX_PATH];
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    static const WCHAR subkey[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages\\";
    WCHAR full[576];
    SIZE_T i;

    if (path[0]) return path;

    lstrcpyW( full, subkey );
    lstrcatW( full, source_package_full_name );
    for (i = 0; i < ARRAY_SIZE(roots); i++)
    {
        HKEY key;
        DWORD type, size = sizeof(path);
        if (!RegOpenKeyExW( roots[i], full, 0, KEY_READ, &key ))
        {
            if (!RegQueryValueExW( key, L"PackagePath", 0, &type, (BYTE *)path, &size )
                && type == REG_SZ && size)
            {
                RegCloseKey( key );
                return path;
            }
            RegCloseKey( key );
        }
    }
    lstrcpyW( path, L"C:\\ProgramData\\winget\\source" );
    return path;
}

/* --------------------------------------------------------------------- */
/* version reference: IReference<Windows.Foundation.Version> answering the
 * version-compare probe (IPackage2 probe slot 8) and the Id().Version() probe. */

struct version_ref
{
    const void **vtbl;
    LONG ref;
};
static struct version_ref *version_ref_impl( struct version_ref *iface ) { return iface; }

static HRESULT WINAPI version_ref_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "version_ref_QueryInterface\n" );
    struct version_ref *impl = version_ref_impl( iface );
    *out = impl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI version_ref_AddRef( void *iface )
{
    struct version_ref *impl = version_ref_impl( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI version_ref_Release( void *iface )
{
    struct version_ref *impl = version_ref_impl( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI version_ref_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI version_ref_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IReference`1<struct Windows.Foundation.Version>",
                                74, name );
}
static HRESULT WINAPI version_ref_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
/* slot 6: IReference<T>::get_Value */
static HRESULT WINAPI version_ref_get_Value( void *iface, void *value )
{
    struct version_ref *impl = version_ref_impl( iface );
    TRACE( "iface %p, value %p\n", impl, value );
    *(struct version_abi *)value = source_version;
    return S_OK;
}
/* slot 7: IReference<T>::get_Type (also used as Value by some projections) */
static HRESULT WINAPI version_ref_get_Type( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Version", 28, name );
}
static HRESULT WINAPI dstr_get_Path( void *iface, HSTRING *value );
static const void *version_ref_vtbl[] =
{
    version_ref_QueryInterface,
    version_ref_AddRef,
    version_ref_Release,
    version_ref_GetIids,
    version_ref_GetRuntimeClassName,
    version_ref_GetTrustLevel,
    version_ref_get_Value,
    version_ref_get_Type,
    version_ref_get_Value,   /* slot 8: also answers struct-return probes */    dstr_get_Path,                /* pad 9 */
    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct version_ref version_ref_obj = { version_ref_vtbl, 1 };

/* --------------------------------------------------------------------- */
/* IPackageId for the source package */

struct source_pkgid
{
    const void **vtbl;
    LONG ref;
};
static struct source_pkgid *pkgid_impl( struct source_pkgid *iface ) { return iface; }

static HRESULT WINAPI pkgid_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "pkgid_QueryInterface\n" );
    struct source_pkgid *impl = pkgid_impl( iface );
    *out = impl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI pkgid_AddRef( void *iface )
{
    struct source_pkgid *impl = pkgid_impl( iface );
    return InterlockedIncrement( &impl->ref );
}
static ULONG WINAPI pkgid_Release( void *iface )
{
    struct source_pkgid *impl = pkgid_impl( iface );
    return InterlockedDecrement( &impl->ref );
}
static HRESULT WINAPI pkgid_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI pkgid_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.PackageId", 36, name );
}
static HRESULT WINAPI pkgid_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI pkgid_get_FullName( void *iface, HSTRING *value )
{
    TRACE( "pkgid_get_FullName\n" );
    return WindowsCreateString( source_package_full_name, ARRAY_SIZE(source_package_full_name) - 1, value );
}
static HRESULT WINAPI pkgid_get_PackageFamilyName( void *iface, HSTRING *value )
{
    TRACE( "pkgid_get_PackageFamilyName\n" );
    return WindowsCreateString( source_package_full_name, ARRAY_SIZE(source_package_full_name) - 1, value );
}
static HRESULT WINAPI pkgid_get_PublisherId( void *iface, HSTRING *value )
{
    return WindowsCreateString( source_publisher_id, ARRAY_SIZE(source_publisher_id) - 1, value );
}
static HRESULT WINAPI pkgid_get_PackageName( void *iface, HSTRING *value )
{
    return WindowsCreateString( source_package_name, ARRAY_SIZE(source_package_name) - 1, value );
}
static HRESULT WINAPI pkgid_get_Version( void *iface, void **value )
{
    struct source_pkgid *impl = pkgid_impl( iface );
    TRACE( "iface %p, value %p\n", impl, value );
    InterlockedIncrement( &version_ref_obj.ref );
    *value = &version_ref_obj;
    return S_OK;
}
static const void *pkgid_vtbl[] =
{
    pkgid_QueryInterface,        /* 0 */
    pkgid_AddRef,                /* 1 */
    pkgid_Release,               /* 2 */
    pkgid_GetIids,               /* 3 */
    pkgid_GetRuntimeClassName,   /* 4 */
    pkgid_GetTrustLevel,         /* 5 */
    pkgid_get_FullName,          /* 6 */
    pkgid_get_PackageFamilyName, /* 7 */
    pkgid_get_PublisherId,       /* 8 */
    pkgid_get_PackageName,       /* 9 */
    pkgid_get_Version,           /* 10 */    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct source_pkgid source_pkgid_obj = { pkgid_vtbl, 1 };

/* Some projections reach the version through InstalledLocation[6] ->
 * "Id()" -> [7] -> struct.  A one-slot-forwarding shim. */
struct pkgid_shim
{
    const void **vtbl;
    LONG ref;
};
static HRESULT WINAPI shim_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( (LONG *)((char *)iface + 8) );
    return S_OK;
}
static ULONG WINAPI shim_AddRef( void *iface ) { return InterlockedIncrement( (LONG *)((char *)iface + 8) ); }
static ULONG WINAPI shim_Release( void *iface ) { return InterlockedDecrement( (LONG *)((char *)iface + 8) ); }
static const void *pkgid_shim_vtbl[] =
{
    shim_QueryInterface,
    shim_AddRef,
    shim_Release,
    pkgid_GetIids,
    pkgid_GetRuntimeClassName,
    pkgid_GetTrustLevel,
    pkgid_get_FullName,  /* 6 */
    pkgid_get_Version,   /* 7 */
    pkgid_get_PublisherId,
    pkgid_get_PackageName,
    pkgid_get_Version,    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct pkgid_shim pkgid_shim_obj = { pkgid_shim_vtbl, 1 };

/* --------------------------------------------------------------------- */
/* InstalledLocation "dstring folder": vtable pointer at +0 whose high dword
 * equals the string length, inline UTF-16 characters at +8. */

/* HSTRING-header compatible: vtable pointer at +0 (its high dword doubles as the
 * string length), character data pointer at +8. */
struct dstr_folder
{
    void **vtbl;          /* vtable page mapped at (strlen << 32): its high
                           * dword doubles as the HSTRING length at +4 */
    LONG flags;           /* winrt_string flags word, unused */
    WCHAR *chars;         /* HSTRING raw buffer pointer at +16 */
    LONG ref;             /* refcount at +24 — consumer Release decrements it */
};
static struct dstr_folder *dstr_folder_obj;

static struct dstr_folder *dstr_impl( struct dstr_folder *iface ) { return iface; }

static LONG *dstr_ref( struct dstr_folder *impl ) { return &impl->ref; }

struct source_package
{
    const void **vtbl;
    LONG ref;
};
static struct source_package source_package_obj;

static HRESULT WINAPI dstr_QueryInterface( void *iface, REFIID iid, void **out )
{
    static const GUID IID_IPackage2 =
        { 0x65aed1ae, 0xb95b, 0x450c, {0x88,0x2b,0x62,0x55,0x18,0x7f,0x39,0x7e} };
    static const GUID IID_IPackage =
        { 0xc312da8d, 0xc82f, 0x51c9, {0xb7,0x05,0x5a,0x26,0xd5,0x2f,0xbe,0xe6} };
    struct dstr_folder *impl = dstr_impl( iface );
    TRACE( "dstr QI %s\n", debugstr_guid( iid ) );
    if (IsEqualGUID( iid, &IID_IPackage2 ))
    {
        InterlockedIncrement( &version_ref_obj.ref );
        *out = &version_ref_obj;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IPackage ))
    {
        InterlockedIncrement( dstr_ref( impl ) );
        *out = impl; /* IPropertyValue face: same dual dstring */
        return S_OK;
    }
    *out = impl;
    InterlockedIncrement( dstr_ref( impl ) );
    return S_OK;
}
static ULONG WINAPI dstr_AddRef( void *iface )
{
    struct dstr_folder *impl = dstr_impl( iface );
    return InterlockedIncrement( dstr_ref( impl ) );
}
static ULONG WINAPI dstr_Release( void *iface )
{
    struct dstr_folder *impl = dstr_impl( iface );
    return InterlockedDecrement( dstr_ref( impl ) );
}
static HRESULT WINAPI dstr_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI dstr_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Storage.StorageFolder", 33, name );
}
static HRESULT WINAPI dstr_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static struct dstr_folder *create_dstr_folder( const WCHAR *str );
static HRESULT WINAPI dstr_get_Name( void *iface, HSTRING *value )
{
    InterlockedIncrement( dstr_ref( dstr_impl( iface ) ) );
    *value = (HSTRING)iface; /* self-describing dual dstring */
    return S_OK;
}
static struct dstr_folder *dstr_path_obj;
static HRESULT WINAPI dstr_get_Path( void *iface, HSTRING *value )
{
    if (!dstr_path_obj) dstr_path_obj = create_dstr_folder( source_package_path() );
    InterlockedIncrement( dstr_ref( dstr_path_obj ) );
    *value = (HSTRING)dstr_path_obj;
    return S_OK;
}
/* slot 6: projections that treat InstalledLocation as Id-carrier */
static HRESULT WINAPI dstr_get_Id( void *iface, void **value )
{
    TRACE( "dstr_get_Id\n" );
    InterlockedIncrement( &pkgid_shim_obj.ref );
    *value = &pkgid_shim_obj;
    return S_OK;
}
static HRESULT WINAPI dstr_IsFramework( void *iface, boolean *value )
{
    *value = false;
    return S_OK;
}
static HRESULT WINAPI dstr_IsResource( void *iface, boolean *value )
{
    *value = false;
    return S_OK;
}
/* slot 8: IPackage2 probe used by the source-version check */
static HRESULT WINAPI dstr_GetSourceInfo( void *iface, void **value )
{
    InterlockedIncrement( &version_ref_obj.ref );
    *value = &version_ref_obj;
    return S_OK;
}
static HRESULT WINAPI dstr_get_FamilyName( void *iface, HSTRING *value )
{
    InterlockedIncrement( dstr_ref( dstr_impl( iface ) ) );
    *value = (HSTRING)iface; /* PFN string: same dual dstring */
    return S_OK;
}
static HRESULT WINAPI dstr_get_InstallPath( void *iface, HSTRING *value )
{
    return dstr_get_Path( iface, value );
}

/* Build the dstring folder: allocate the vtable page at (len << 32) so the
 * high dword of the vtable pointer reads as the string length. */
static void *dstr_vtbl_page[16];
static struct dstr_folder *create_dstr_folder( const WCHAR *str )
{
    struct dstr_folder *obj;
    DWORD len = lstrlenW( str );
    void *base;
    SIZE_T i;

    obj = heap_alloc_zero( sizeof(*obj) );
    if (!obj) return NULL;

    /* MEM_COMMIT over an unmapped hinted address yields the page; 64-bit
     * Wine honors the hint.  If it moves, the name compare just misses. */
    base = VirtualAlloc( (void *)((UINT64)len << 32), sizeof(dstr_vtbl_page), MEM_COMMIT | MEM_RESERVE,
                         PAGE_READWRITE );
    TRACE( "dstr vtable base %p (len %lu)\n", base, len );
    if (base != (void *)((UINT64)len << 32))
        ERR( "vtable base %p != hinted %p; InstalledLocation name match will miss\n",
             base, (void *)((UINT64)len << 32) );

    dstr_vtbl_page[0] = dstr_QueryInterface;
    dstr_vtbl_page[1] = dstr_AddRef;
    dstr_vtbl_page[2] = dstr_Release;
    dstr_vtbl_page[3] = dstr_GetIids;
    dstr_vtbl_page[4] = dstr_GetRuntimeClassName;
    dstr_vtbl_page[5] = dstr_GetTrustLevel;
    dstr_vtbl_page[6] = dstr_get_Id;
    dstr_vtbl_page[7] = dstr_get_Path;
    dstr_vtbl_page[8] = dstr_GetSourceInfo;
    dstr_vtbl_page[9] = dstr_get_FamilyName;
    dstr_vtbl_page[10] = dstr_get_InstallPath;
    for (i = 11; i < ARRAY_SIZE(dstr_vtbl_page); i++)
        dstr_vtbl_page[i] = dstr_get_Path;

    if (base) memcpy( base, dstr_vtbl_page, sizeof(dstr_vtbl_page) );
    obj->vtbl = base ? base : dstr_vtbl_page;
    obj->chars = heap_alloc_zero( (len + 1) * sizeof(WCHAR) );
    if (!obj->chars) { heap_free( obj ); return NULL; }
    lstrcpyW( obj->chars, str );
    *dstr_ref( obj ) = 1;
    return obj;
}

/* Standard-layout StorageFolder (no dstring trick) for IPackage[7]. */
struct folder
{
    const void **vtbl;
    LONG ref;
};
static HRESULT WINAPI folder_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "folder QI %s\n", debugstr_guid( iid ) );
    *out = iface;
    InterlockedIncrement( (LONG *)((char *)iface + 8) );
    return S_OK;
}
static ULONG WINAPI folder_AddRef( void *iface ) { return InterlockedIncrement( (LONG *)((char *)iface + 8) ); }
static ULONG WINAPI folder_Release( void *iface ) { return InterlockedDecrement( (LONG *)((char *)iface + 8) ); }
static const void *folder_vtbl[] =
{
    folder_QueryInterface,
    folder_AddRef,
    folder_Release,
    dstr_GetIids,
    dstr_GetRuntimeClassName,
    dstr_GetTrustLevel,
    dstr_get_Name,
    dstr_get_Path,
    dstr_IsFramework,    dstr_get_Path,                /* pad 9 */
    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct folder folder_obj = { folder_vtbl, 1 };

/* --------------------------------------------------------------------- */
/* IPackage for the source package: slot 9 is the hot one (winget v1.29
 * calls [9] for InstalledLocation/Id; [7] for InstalledLocation standard). */

struct source_package
{
    const void **vtbl;
    LONG ref;
};
static HRESULT WINAPI package_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "package QI %s\n", debugstr_guid( iid ) );
    *out = iface;
    InterlockedIncrement( (LONG *)((char *)iface + 8) );
    return S_OK;
}
static ULONG WINAPI package_AddRef( void *iface ) { return InterlockedIncrement( (LONG *)((char *)iface + 8) ); }
static ULONG WINAPI package_Release( void *iface ) { return InterlockedDecrement( (LONG *)((char *)iface + 8) ); }
static HRESULT WINAPI package_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI package_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.Package", 36, name );
}
static HRESULT WINAPI package_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI package_get_Id( void *iface, void **value )
{
    TRACE( "package_get_Id\n" );
    InterlockedIncrement( &source_pkgid_obj.ref );
    *value = &source_pkgid_obj;
    return S_OK;
}
static HRESULT WINAPI package_get_InstalledLocation( void *iface, void **value )
{
    TRACE( "package_get_InstalledLocation\n" );
    InterlockedIncrement( &folder_obj.ref );
    *value = &folder_obj;
    return S_OK;
}
static HRESULT WINAPI package_get_InstalledLocationD( void *iface, void **value )
{
    TRACE( "package_get_InstalledLocationD\n" );
    if (!dstr_folder_obj) dstr_folder_obj = create_dstr_folder( source_package_full_name );
    InterlockedIncrement( dstr_ref( dstr_folder_obj ) );
    *value = dstr_folder_obj;
    return S_OK;
}
/* IPackage v6 string getters (DisplayName/Description/Logo) as dual dstrings. */
static HRESULT WINAPI package_get_DisplayName( void *iface, HSTRING *value )
{
    static struct dstr_folder *dn;
    if (!dn) dn = create_dstr_folder( source_package_full_name );
    InterlockedIncrement( dstr_ref( dn ) );
    *value = (HSTRING)dn;
    return S_OK;
}
static HRESULT WINAPI package_get_Description( void *iface, HSTRING *value )
{
    static struct dstr_folder *de;
    if (!de) de = create_dstr_folder( source_package_full_name );
    InterlockedIncrement( dstr_ref( de ) );
    *value = (HSTRING)de;
    return S_OK;
}
static HRESULT WINAPI package_get_Logo( void *iface, HSTRING *value )
{
    static struct dstr_folder *lo;
    TRACE( "package_get_Logo\n" );
    if (!lo) lo = create_dstr_folder( L"C:\\ProgramData\\winget\\source\\Logo.png" );
    InterlockedIncrement( dstr_ref( lo ) );
    *value = (HSTRING)lo;
    return S_OK;
}
static const void *package_vtbl[] =
{
    package_QueryInterface,           /* 0 */
    package_AddRef,                   /* 1 */
    package_Release,                  /* 2 */
    package_GetIids,                  /* 3 */
    package_GetRuntimeClassName,      /* 4 */
    package_GetTrustLevel,            /* 5 */
    package_get_Id,                   /* 6 */
    package_get_InstalledLocation,    /* 7 */
    dstr_IsFramework,                 /* 8 */
    package_get_InstalledLocationD,   /* 9 - the slot the v1.29 projection uses */
    dstr_IsResource,                  /* 10 - Status */
    dstr_IsFramework,                 /* 11 - DeployPackage stub */
    dstr_IsResource,                  /* 12 - IsResourcePackage */
    package_get_DisplayName,          /* 13 */
    package_get_Description,          /* 14 */
    package_get_Logo,                 /* 15 - IPackage v6 (22H2) */
};
static struct source_package source_package_obj = { package_vtbl, 1 };

/* --------------------------------------------------------------------- */
/* AppExtension: [6] Id (HSTRING "IndexDB"), [10] Package */

struct app_extension
{
    const void **vtbl;
    LONG ref;
};
static HRESULT WINAPI appext_QueryInterface( void *iface, REFIID iid, void **out )
{
    TRACE( "appext QI %s\n", debugstr_guid( iid ) );
    *out = iface;
    InterlockedIncrement( (LONG *)((char *)iface + 8) );
    return S_OK;
}
static ULONG WINAPI appext_AddRef( void *iface ) { return InterlockedIncrement( (LONG *)((char *)iface + 8) ); }
static ULONG WINAPI appext_Release( void *iface ) { return InterlockedDecrement( (LONG *)((char *)iface + 8) ); }
static HRESULT WINAPI appext_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI appext_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.AppExtensions.AppExtension", 53, name );
}
static HRESULT WINAPI appext_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI appext_get_Id( void *iface, HSTRING *value )
{
    TRACE( "appext_get_Id\n" );
    return WindowsCreateString( source_extension_id, ARRAY_SIZE(source_extension_id) - 1, value );
}
static HRESULT WINAPI appext_get_Package( void *iface, void **value )
{
    TRACE( "appext_get_Package\n" );
    InterlockedIncrement( &source_package_obj.ref );
    *value = &source_package_obj;
    return S_OK;
}
static const void *appext_vtbl[] =
{
    appext_QueryInterface,      /* 0 */
    appext_AddRef,
    appext_Release,
    appext_GetIids,
    appext_GetRuntimeClassName,
    appext_GetTrustLevel,
    appext_get_Id,              /* 6 */
    appext_get_Package,         /* 7 - IAppExtension layout */
    dstr_get_InstallPath,       /* 8 - IAppExtension2 Registry stub */
    dstr_get_InstallPath,       /* 9 */
    appext_get_Package,         /* 10 - v1.29 projection slot */
    appext_get_Package,         /* 11 */
    appext_get_Id,              /* 12 */
    appext_get_Id,              /* 13 */
    appext_get_Id,              /* 14 */
    appext_get_Package,         /* 15 */
};
static struct app_extension app_extension_obj = { appext_vtbl, 1 };

/* --------------------------------------------------------------------- */
/* IVectorView<AppExtension> of exactly one extension */

struct appext_iterator
{
    const void **vtbl;
    LONG ref;
    boolean done;
};

struct appext_view
{
    const void **vtbl;
    LONG ref;
};

static HRESULT WINAPI view_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( (LONG *)((char *)iface + 8) );
    return S_OK;
}
static ULONG WINAPI view_AddRef( void *iface ) { return InterlockedIncrement( (LONG *)((char *)iface + 8) ); }
static ULONG WINAPI view_Release( void *iface ) { return InterlockedDecrement( (LONG *)((char *)iface + 8) ); }
static HRESULT WINAPI view_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}
static HRESULT WINAPI view_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.AppExtensions.AppExtension>",
                                105, name );
}
static HRESULT WINAPI view_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI view_GetAt( void *iface, UINT32 index, void **item )
{
    TRACE( "view_GetAt\n" );
    if (index > 0) return E_INVALIDARG;
    InterlockedIncrement( &app_extension_obj.ref );
    *item = &app_extension_obj;
    return S_OK;
}
static HRESULT WINAPI view_get_Size( void *iface, UINT32 *size )
{
    TRACE( "view_get_Size\n" );
    *size = 1;
    return S_OK;
}
static HRESULT WINAPI view_IndexOf( void *iface, void *item, UINT32 *index, boolean *found )
{
    TRACE( "view_IndexOf\n" );
    *index = 0;
    *found = (item == (void *)&app_extension_obj);
    return S_OK;
}
/* iterator */
static HRESULT WINAPI iter_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &((struct appext_iterator *)iface)->ref );
    return S_OK;
}
static ULONG WINAPI iter_AddRef( void *iface ) { return InterlockedIncrement( &((struct appext_iterator *)iface)->ref ); }
static ULONG WINAPI iter_Release( void *iface ) { return InterlockedDecrement( &((struct appext_iterator *)iface)->ref ); }
static HRESULT WINAPI iter_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI iter_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.Collections.IIterator`1<Windows.ApplicationModel.AppExtensions.AppExtension>",
                                107, name );
}
static HRESULT WINAPI iter_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI iter_get_Current( void *iface, void **item )
{
    struct appext_iterator *impl = (struct appext_iterator *)iface;
    if (impl->done) return E_FAIL;
    InterlockedIncrement( &app_extension_obj.ref );
    *item = &app_extension_obj;
    return S_OK;
}
static HRESULT WINAPI iter_get_HasCurrent( void *iface, boolean *value )
{
    struct appext_iterator *impl = (struct appext_iterator *)iface;
    *value = !impl->done;
    return S_OK;
}
static HRESULT WINAPI iter_MoveNext( void *iface, boolean *value )
{
    struct appext_iterator *impl = (struct appext_iterator *)iface;
    impl->done = true;
    *value = false;
    return S_OK;
}
static HRESULT WINAPI iter_GetMany( void *iface, UINT32 count, void *items, UINT32 *read )
{
    *read = 0;
    return S_OK;
}
static const void *iter_vtbl[] =
{
    iter_QueryInterface,
    iter_AddRef,
    iter_Release,
    iter_GetIids,
    iter_GetRuntimeClassName,
    iter_GetTrustLevel,
    iter_MoveNext,       /* 6 */
    iter_get_HasCurrent, /* 7 */
    iter_GetMany,        /* 8 */
    iter_get_Current,    /* 9 */    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static HRESULT WINAPI view_First( void *iface, void **first );

static const void *view_vtbl[] =
{
    view_QueryInterface,
    view_AddRef,
    view_Release,
    view_GetIids,
    view_GetRuntimeClassName,
    view_GetTrustLevel,
    view_GetAt,          /* 6 */
    view_get_Size,       /* 7 */
    view_IndexOf,        /* 8 */
    view_First,          /* 9 */    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct appext_view appext_view_obj = { view_vtbl, 1 };

/* First(): returns a fresh iterator; simple singleton iterator. */
static struct appext_iterator appext_iter_obj = { iter_vtbl, 1, false };
static HRESULT WINAPI view_First( void *iface, void **first )
{
    TRACE( "view_First\n" );
    appext_iter_obj.done = false;
    InterlockedIncrement( &appext_iter_obj.ref );
    *first = &appext_iter_obj;
    return S_OK;
}

/* --------------------------------------------------------------------- */
/* IAsyncOperation<IVectorView<AppExtension*>> — completes synchronously */

struct async_op
{
    const void **vtbl;
    LONG ref;
    LONG status;   /* AsyncStatus: 0 Started, 1 Completed */
    IUnknown *handler;
    void **handler_vtbl; /* NULL for a default-constructed handler slot */
    IUnknown *completed; /* handler handed out by get_Completed */
};

/* C++/WinRT .get() calls get_Completed() first and unwraps the operation's
 * existing completion handler; the op must own one from birth.  A static
 * no-op delegate answers, QI'ing to self so slot 7 doubles as get_Status. */
struct dummy_handler
{
    const void **vtbl;
    LONG ref;
};
static const void *dummy_handler_vtbl[];
static struct dummy_handler dummy_handler_obj = { dummy_handler_vtbl, 1 };

static HRESULT WINAPI dummy_QueryInterface( void *iface, REFIID iid, void **out )
{
    *out = iface;
    InterlockedIncrement( &((struct dummy_handler *)iface)->ref );
    return S_OK;
}
static ULONG WINAPI dummy_AddRef( void *iface ) { return InterlockedIncrement( &((struct dummy_handler *)iface)->ref ); }
static ULONG WINAPI dummy_Release( void *iface ) { return InterlockedDecrement( &((struct dummy_handler *)iface)->ref ); }
static HRESULT WINAPI dummy_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI dummy_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.AsyncOperationCompletedHandler`1<Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.AppExtensions.AppExtension>>",
                                 152, name );
}
static HRESULT WINAPI dummy_GetTrustLevel( void *iface, TrustLevel *level ) { *level = BaseTrust; return S_OK; }
static HRESULT WINAPI dummy_Invoke( void *iface, void *op, enum AsyncStatus status ) { return S_OK; }
static HRESULT WINAPI dummy_get_Status( void *iface, enum AsyncStatus *status )
{
    TRACE( "dummy_get_Status\n" );
    *status = 1; /* Completed */
    return S_OK;
}
/* The winget .get() helper reuses the object returned by get_Completed as the
 * IAsyncOperation itself: QI -> slot 7 get_Status -> slot 8 GetResults.
 * Answer with the singleton view. */
static HRESULT WINAPI dummy_GetResults( void *iface, void **results )
{
    TRACE( "dummy_GetResults\n" );
    InterlockedIncrement( &appext_view_obj.ref );
    *results = &appext_view_obj;
    return S_OK;
}
static HRESULT WINAPI dummy_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI dummy_Close( void *iface ) { return S_OK; }
static const void *dummy_handler_vtbl[] =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
    dummy_GetIids,
    dummy_GetRuntimeClassName,
    dummy_GetTrustLevel,
    dummy_Invoke,     /* 6 IAsyncOperationCompletedHandler::Invoke */
    dummy_get_Status, /* 7 doubles as IAsyncInfo::get_Status for .get() */
    dummy_GetResults, /* 8 IAsyncOperation::GetResults — consumer unwraps handler as op */
    dummy_Cancel,     /* 9 IAsyncInfo::Cancel */
    dummy_Close,      /* 10 IAsyncInfo::Close */    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};

static const void *async_op_vtbl[];
static const void *async_info_vtbl[];

struct async_info
{
    const void **vtbl;
    struct async_op *op;
};

static struct async_op *async_from_info( struct async_info *iface ) { return iface->op; }

static HRESULT WINAPI async_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct async_op *impl = (struct async_op *)iface;
    static const GUID IID_IAsyncInfo =
        { 0x00000036, 0x0000, 0x0000, {0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46} };
    if (IsEqualGUID( iid, &IID_IAsyncInfo ))
    {
        *out = (struct async_info *)((char *)impl + sizeof(struct async_op));
        InterlockedIncrement( &impl->ref );
        return S_OK;
    }
    *out = impl;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI async_AddRef( void *iface )
{
    return InterlockedIncrement( &((struct async_op *)iface)->ref );
}
static ULONG WINAPI async_Release( void *iface )
{
    struct async_op *impl = (struct async_op *)iface;
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler && impl->handler_vtbl)
            ((ULONG (WINAPI *)( void * ))impl->handler_vtbl[2])( impl->handler ); /* Release */
        heap_free( impl );
    }
    return ref;
}
static HRESULT WINAPI async_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI async_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.AppExtensions.AppExtension>>",
                                152, name );
}
static HRESULT WINAPI async_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
/* winget calls put_Completed with a default-constructed C++/WinRT handler:
 * a pointer to an interface-pointer slot whose vtable entry is NULL.  Only
 * AddRef/invoke when the slot actually carries a delegate. */
static HRESULT WINAPI async_put_Completed( void *iface, void *handler )
{
    TRACE( "async_put_Completed\n" );
    struct async_op *impl = (struct async_op *)iface;
    void **vtbl = handler ? *(void ***)handler : NULL;
    TRACE( "iface %p, handler %p, vtbl %p\n", impl, handler, vtbl );
    if (impl->handler && impl->handler_vtbl)
        ((ULONG (WINAPI *)( void * ))impl->handler_vtbl[2])( impl->handler ); /* Release */
    impl->handler = (IUnknown *)handler;
    impl->handler_vtbl = vtbl;
    if (vtbl) ((ULONG (WINAPI *)( void * ))vtbl[1])( handler ); /* AddRef */
    impl->status = 1; /* Completed */
    if (vtbl) ((void (WINAPI *)( void *, void *, enum AsyncStatus ))vtbl[6])( handler, iface, 1 /* Completed */ );
    return S_OK;
}
static HRESULT WINAPI async_get_Completed( void *iface, void **out )
{
    TRACE( "async_get_Completed\n" );
    struct async_op *impl = (struct async_op *)iface;
    void **vtbl;
    *out = (void *)impl->completed;
    if ((vtbl = *out ? *(void **)*out : NULL))
        ((ULONG (WINAPI *)( void * ))vtbl[1])( *out ); /* AddRef via vtbl slot 1 */
    return S_OK;
}
static HRESULT WINAPI async_GetResults( void *iface, void **results )
{
    TRACE( "async_GetResults\n" );
    InterlockedIncrement( &appext_view_obj.ref );
    *results = &appext_view_obj;
    return S_OK;
}
/* IAsyncInfo */
static HRESULT WINAPI ainfo_QueryInterface( void *iface, REFIID iid, void **out )
{
    struct async_op *impl = async_from_info( (struct async_info *)iface );
    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI ainfo_AddRef( void *iface )
{
    return InterlockedIncrement( &async_from_info( (struct async_info *)iface )->ref );
}
static ULONG WINAPI ainfo_Release( void *iface )
{
    struct async_op *impl = async_from_info( (struct async_info *)iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler && impl->handler_vtbl)
            ((ULONG (WINAPI *)( void * ))impl->handler_vtbl[2])( impl->handler ); /* Release */
        heap_free( impl );
    }
    return ref;
}
static HRESULT WINAPI ainfo_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI ainfo_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.Foundation.IAsyncInfo", 31, name );
}
static HRESULT WINAPI ainfo_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI ainfo_get_Id( void *iface, UINT32 *id )
{
    *id = 1;
    return S_OK;
}
static HRESULT WINAPI ainfo_get_Status( void *iface, enum AsyncStatus *status )
{
    *status = async_from_info( (struct async_info *)iface )->status;
    return S_OK;
}
static HRESULT WINAPI ainfo_get_ErrorCode( void *iface, HRESULT *code )
{
    *code = S_OK;
    return S_OK;
}
static HRESULT WINAPI ainfo_Cancel( void *iface ) { return S_OK; }
static HRESULT WINAPI ainfo_Close( void *iface ) { return S_OK; }

static const void *async_op_vtbl[] =
{
    async_QueryInterface,       /* 0 */
    async_AddRef,
    async_Release,
    async_GetIids,
    async_GetRuntimeClassName,
    async_GetTrustLevel,
    async_get_Completed,        /* 6 (winrt IAsyncOperation ABI: 6 get, 7 put) */
    async_put_Completed,        /* 7 */
    async_GetResults,           /* 8 */    dstr_get_Path,                /* pad 9 */
    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static const void *async_info_vtbl[] =
{
    ainfo_QueryInterface,       /* 0 */
    ainfo_AddRef,
    ainfo_Release,
    ainfo_GetIids,
    ainfo_GetRuntimeClassName,
    ainfo_GetTrustLevel,
    ainfo_get_Id,               /* 6 */
    ainfo_get_Status,           /* 7 */
    ainfo_get_ErrorCode,        /* 8 */
    ainfo_Cancel,               /* 9 */
    ainfo_Close,                /* 10 */    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct async_op *create_async_op( void )
{
    struct async_op *op = heap_alloc_zero( sizeof(*op) + sizeof(struct async_info) );
    if (!op) return NULL;
    op->vtbl = async_op_vtbl;
    op->ref = 1;
    op->status = 1; /* Completed */
    op->handler = NULL;
    op->handler_vtbl = NULL;
    op->completed = (IUnknown *)&dummy_handler_obj;
    ((struct async_info *)(op + 1))->vtbl = async_info_vtbl;
    ((struct async_info *)(op + 1))->op = op;
    return op;
}

/* --------------------------------------------------------------------- */
/* AppExtensionCatalog activation factory + statics */

struct catalog_factory
{
    const void **vtbl;
    LONG ref;
};
static const GUID IID_IAppExtensionCatalogStatics =
    { 0x3c36668a, 0x5f18, 0x4f0b, {0x9c,0xe5,0xca,0xb6,0x1d,0x19,0x6f,0x11} };
static const GUID IID_IAppExtensionCatalogStatics2 =
    { 0x94ea2b94, 0xe9cc, 0x49e0, {0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90} };

static const void *catalog_statics_vtbl[];
static const void *catalog_statics2_vtbl[];
static struct catalog_factory catalog_statics_obj = { catalog_statics_vtbl, 1 };
static struct catalog_factory catalog_statics2_obj = { catalog_statics2_vtbl, 1 };

/* winget v1.29 probes IAppExtensionCatalogStatics2 and calls its slot 6 with
 * (this, IAsyncOperation**) = FindAllAsync; plain statics slot 6 is
 * Open(HSTRING, IAsyncOperation**).  Hand out distinct objects so each slot 6
 * gets its own signature. */
static HRESULT WINAPI catalog_QueryInterface( void *iface, REFIID iid, void **out )
{
    if (IsEqualGUID( iid, &IID_IAppExtensionCatalogStatics ))
    {
        *out = &catalog_statics_obj;
        InterlockedIncrement( &catalog_statics_obj.ref );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IAppExtensionCatalogStatics2 ))
    {
        *out = &catalog_statics2_obj;
        InterlockedIncrement( &catalog_statics2_obj.ref );
        return S_OK;
    }
    *out = iface;
    InterlockedIncrement( (LONG *)((char *)iface + 8) );
    return S_OK;
}
static ULONG WINAPI catalog_AddRef( void *iface ) { return InterlockedIncrement( (LONG *)((char *)iface + 8) ); }
static ULONG WINAPI catalog_Release( void *iface ) { return InterlockedDecrement( (LONG *)((char *)iface + 8) ); }
static HRESULT WINAPI catalog_GetIids( void *iface, ULONG *count, IID **iids )
{
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI catalog_GetRuntimeClassName( void *iface, HSTRING *name )
{
    return WindowsCreateString( L"Windows.ApplicationModel.AppExtensions.AppExtensionCatalog", 58, name );
}
static HRESULT WINAPI catalog_GetTrustLevel( void *iface, TrustLevel *level )
{
    *level = BaseTrust;
    return S_OK;
}
/* IAppExtensionCatalogStatics slot 6: Open(name) */
static HRESULT WINAPI catalog_Open( void *iface, HSTRING name, void **op )
{
    TRACE( "catalog_Open\n" );
    struct async_op *async = create_async_op();
    TRACE( "name %s, op %p\n", debugstr_hstring( name ), op );
    if (!async) return E_OUTOFMEMORY;
    *op = async;
    return S_OK;
}

/* IAppExtensionCatalogStatics2 slot 6: FindAllAsync() */
static HRESULT WINAPI catalog_FindAllAsync( void *iface, void **op )
{
    struct async_op *async = create_async_op();
    TRACE( "op %p\n", op );
    if (!async) return E_OUTOFMEMORY;
    *op = async;
    return S_OK;
}

/* The activation factory hands out the statics object; QI on it dispatches by
 * IID to statics (Open) or statics2 (FindAllAsync). */
static HRESULT WINAPI catalog_ActivateInstance( void *iface, void **instance )
{
    InterlockedIncrement( &catalog_statics_obj.ref );
    *instance = &catalog_statics_obj;
    return S_OK;
}
static const void *catalog_vtbl[] =
{
    catalog_QueryInterface,        /* 0 */
    catalog_AddRef,                /* 1 */
    catalog_Release,               /* 2 */
    catalog_GetIids,               /* 3 */
    catalog_GetRuntimeClassName,   /* 4 */
    catalog_GetTrustLevel,         /* 5 */
    catalog_ActivateInstance,      /* 6 (IActivationFactory) */    dstr_get_Path,                /* pad 7 */
    dstr_get_Path,                /* pad 8 */
    dstr_get_Path,                /* pad 9 */
    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};
static struct catalog_factory catalog_factory_obj = { catalog_vtbl, 1 };

static const void *catalog_statics_vtbl[] =
{
    catalog_QueryInterface,
    catalog_AddRef,
    catalog_Release,
    catalog_GetIids,
    catalog_GetRuntimeClassName,
    catalog_GetTrustLevel,
    catalog_Open,                  /* 6 (IAppExtensionCatalogStatics::Open) */    dstr_get_Path,                /* pad 7 */
    dstr_get_Path,                /* pad 8 */
    dstr_get_Path,                /* pad 9 */
    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};

static const void *catalog_statics2_vtbl[] =
{
    catalog_QueryInterface,
    catalog_AddRef,
    catalog_Release,
    catalog_GetIids,
    catalog_GetRuntimeClassName,
    catalog_GetTrustLevel,
    catalog_FindAllAsync,          /* 6 (IAppExtensionCatalogStatics2::FindAllAsync) */    dstr_get_Path,                /* pad 7 */
    dstr_get_Path,                /* pad 8 */
    dstr_get_Path,                /* pad 9 */
    dstr_get_Path,                /* pad 10 */
    dstr_get_Path,                /* pad 11 */
    dstr_get_Path,                /* pad 12 */
    dstr_get_Path,                /* pad 13 */
    dstr_get_Path,                /* pad 14 */
    dstr_get_Path,                /* pad 15 */

};

IActivationFactory *app_extension_catalog_factory = (IActivationFactory *)&catalog_factory_obj;
