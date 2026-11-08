/*
 * Implementation of VERSION.DLL
 *
 * Copyright 1996,1997 Marcus Meissner
 * Copyright 1997 David Cuthbert
 * Copyright 1999 Ulrich Weigand
 * Copyright 2005 Paul Vriens
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
 *
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winver.h"
#include "winuser.h"
#include "winnls.h"
#include "winternl.h"
#include "winerror.h"
#include "appmodel.h"

#include "kernelbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ver);

typedef struct
{
    WORD offset;
    WORD length;
    WORD flags;
    WORD id;
    WORD handle;
    WORD usage;
} NE_NAMEINFO;

typedef struct
{
    WORD  type_id;
    WORD  count;
    DWORD resloader;
} NE_TYPEINFO;

struct version_info
{
    DWORD major;
    DWORD minor;
    DWORD build;
};

/***********************************************************************
 * Version Info Structure
 */

typedef struct
{
    WORD  wLength;
    WORD  wValueLength;
    CHAR  szKey[1];
#if 0   /* variable length structure */
    /* DWORD aligned */
    BYTE  Value[];
    /* DWORD aligned */
    VS_VERSION_INFO_STRUCT16 Children[];
#endif
} VS_VERSION_INFO_STRUCT16;

typedef struct
{
    WORD  wLength;
    WORD  wValueLength;
    WORD  wType; /* 1:Text, 0:Binary */
    WCHAR szKey[1];
#if 0   /* variable length structure */
    /* DWORD aligned */
    BYTE  Value[];
    /* DWORD aligned */
    VS_VERSION_INFO_STRUCT32 Children[];
#endif
} VS_VERSION_INFO_STRUCT32;

#define VersionInfoIs16( ver ) \
    ( ((const VS_VERSION_INFO_STRUCT16 *)ver)->szKey[0] >= ' ' )

#define DWORD_ALIGN( base, ptr ) \
    ( (LPBYTE)(base) + ((((LPBYTE)(ptr) - (LPBYTE)(base)) + 3) & ~3) )

#define VersionInfo16_Value( ver )  \
    DWORD_ALIGN( (ver), (ver)->szKey + strlen((ver)->szKey) + 1 )
#define VersionInfo32_Value( ver )  \
    DWORD_ALIGN( (ver), (ver)->szKey + lstrlenW((ver)->szKey) + 1 )

#define VersionInfo16_Children( ver )  \
    (const VS_VERSION_INFO_STRUCT16 *)( VersionInfo16_Value( ver ) + \
                           ( ( (ver)->wValueLength + 3 ) & ~3 ) )
#define VersionInfo32_Children( ver )  \
    (const VS_VERSION_INFO_STRUCT32 *)( VersionInfo32_Value( ver ) + \
                           ( ( (ver)->wValueLength * \
                               ((ver)->wType? 2 : 1) + 3 ) & ~3 ) )

#define VersionInfo16_Next( ver ) \
    (VS_VERSION_INFO_STRUCT16 *)( (LPBYTE)ver + (((ver)->wLength + 3) & ~3) )
#define VersionInfo32_Next( ver ) \
    (VS_VERSION_INFO_STRUCT32 *)( (LPBYTE)ver + (((ver)->wLength + 3) & ~3) )


/***********************************************************************
 * Win8 info, reported if the app doesn't provide compat GUID in the manifest and
 * doesn't have higher OS version in PE header.
 */
static const struct version_info windows8_version_info = { 6, 2, 9200 };

/***********************************************************************
 * Win8.1 info, reported if the app doesn't provide compat GUID in the manifest and
 * OS version in PE header is 8.1 or higher but below 10.
 */
static const struct version_info windows8_1_version_info = { 6, 3, 9600 };


/***********************************************************************
 * Windows versions that need compatibility GUID specified in manifest
 * in order to be reported by the APIs.
 */
static const struct
{
    struct version_info info;
    GUID guid;
} version_data[] =
{
    /* Windows 8.1 */
    {
        { 6, 3, 9600 },
        {0x1f676c76,0x80e1,0x4239,{0x95,0xbb,0x83,0xd0,0xf6,0xd0,0xda,0x78}}
    },
    /* Windows 10 */
    {
        { 10, 0, 19045 },
        {0x8e0f7a12,0xbfb3,0x4fe8,{0xb9,0xa5,0x48,0xfd,0x50,0xa1,0x5a,0x9a}}
    }
};


/******************************************************************************
 *  init_current_version
 *
 * Initialize the current_version variable.
 *
 * For compatibility, Windows 8.1 and later report Win8 version unless the app
 * has a manifest or higher OS version in the PE optional header
 * that confirms its compatibility with newer versions of Windows.
 *
 */
static RTL_OSVERSIONINFOEXW current_version;

static BOOL CALLBACK init_current_version(PINIT_ONCE init_once, PVOID parameter, PVOID *context)
{
    struct acci
    {
        DWORD ElementCount;
        COMPATIBILITY_CONTEXT_ELEMENT Elements[1];
    } *acci;
    BOOL have_os_compat_elements = FALSE;
    const struct version_info *ver;
    IMAGE_NT_HEADERS *nt;
    SIZE_T req;
    int idx;

    current_version.dwOSVersionInfoSize = sizeof(current_version);
    if (!set_ntstatus( RtlGetVersion(&current_version) )) return FALSE;

    for (idx = ARRAY_SIZE(version_data); idx--;)
        if ( current_version.dwMajorVersion >  version_data[idx].info.major ||
            (current_version.dwMajorVersion == version_data[idx].info.major &&
             current_version.dwMinorVersion >= version_data[idx].info.minor))
            break;

    if (idx < 0) return TRUE;
    ver = &windows8_version_info;

    if (RtlQueryInformationActivationContext(0, NtCurrentTeb()->Peb->ActivationContextData, NULL,
            CompatibilityInformationInActivationContext, NULL, 0, &req) != STATUS_BUFFER_TOO_SMALL
        || !req)
        goto done;

    if (!(acci = HeapAlloc(GetProcessHeap(), 0, req)))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    if (RtlQueryInformationActivationContext(0, NtCurrentTeb()->Peb->ActivationContextData, NULL,
            CompatibilityInformationInActivationContext, acci, req, &req) == STATUS_SUCCESS)
    {
        do
        {
            DWORD i;

            for (i = 0; i < acci->ElementCount; i++)
            {
                if (acci->Elements[i].Type != ACTCTX_COMPATIBILITY_ELEMENT_TYPE_OS)
                    continue;

                have_os_compat_elements = TRUE;

                if (IsEqualGUID(&acci->Elements[i].Id, &version_data[idx].guid))
                {
                    ver = &version_data[idx].info;

                    if (ver->major == current_version.dwMajorVersion &&
                        ver->minor == current_version.dwMinorVersion)
                        ver = NULL;

                    idx = 0;  /* break from outer loop */
                    break;
                }
            }
        } while (idx--);
    }
    HeapFree(GetProcessHeap(), 0, acci);

done:
    if (!have_os_compat_elements && current_version.dwMajorVersion >= 10
            && (nt = RtlImageNtHeader(NtCurrentTeb()->Peb->ImageBaseAddress))
            && (nt->OptionalHeader.MajorOperatingSystemVersion > 6
            || (nt->OptionalHeader.MajorOperatingSystemVersion == 6
            && nt->OptionalHeader.MinorOperatingSystemVersion >= 3)))
    {
        if (current_version.dwMajorVersion > 10)
            FIXME("Unsupported current_version.dwMajorVersion %lu.\n", current_version.dwMajorVersion);

        ver = nt->OptionalHeader.MajorOperatingSystemVersion >= 10 ? NULL : &windows8_1_version_info;
    }

    if (ver)
    {
        current_version.dwMajorVersion = ver->major;
        current_version.dwMinorVersion = ver->minor;
        current_version.dwBuildNumber  = ver->build;
    }
    return TRUE;
}


/**********************************************************************
 *  find_entry_by_id
 *
 * Find an entry by id in a resource directory
 * Copied from loader/pe_resource.c
 */
static const IMAGE_RESOURCE_DIRECTORY *find_entry_by_id( const IMAGE_RESOURCE_DIRECTORY *dir,
                                                         WORD id, const void *root,
                                                         DWORD root_size )
{
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *entry;
    int min, max, pos;

    entry = (const IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    min = dir->NumberOfNamedEntries;
    max = min + dir->NumberOfIdEntries - 1;

    if (max >= (root_size - ((INT_PTR)dir - (INT_PTR)root) - sizeof(*dir)) / sizeof(*entry))
        return NULL;

    while (min <= max)
    {
        pos = (min + max) / 2;
        if (entry[pos].Id == id)
        {
            DWORD offset = entry[pos].OffsetToDirectory;
            if (offset > root_size - sizeof(*dir)) return NULL;
            return (const IMAGE_RESOURCE_DIRECTORY *)((const char *)root + offset);
        }
        if (entry[pos].Id > id) max = pos - 1;
        else min = pos + 1;
    }
    return NULL;
}


/**********************************************************************
 *  find_entry_default
 *
 * Find a default entry in a resource directory
 * Copied from loader/pe_resource.c
 */
static const IMAGE_RESOURCE_DIRECTORY *find_entry_default( const IMAGE_RESOURCE_DIRECTORY *dir,
                                                           const void *root )
{
    const IMAGE_RESOURCE_DIRECTORY_ENTRY *entry;

    entry = (const IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    return (const IMAGE_RESOURCE_DIRECTORY *)((const char *)root + entry->OffsetToDirectory);
}


/**********************************************************************
 *  push_language
 *
 * push a language onto the list of languages to try
 */
static inline int push_language( WORD *list, int pos, WORD lang )
{
    int i;
    for (i = 0; i < pos; i++) if (list[i] == lang) return pos;
    list[pos++] = lang;
    return pos;
}


/**********************************************************************
 *  find_entry_language
 */
static const IMAGE_RESOURCE_DIRECTORY *find_entry_language( const IMAGE_RESOURCE_DIRECTORY *dir,
                                                            const void *root, DWORD root_size,
                                                            DWORD flags )
{
    const IMAGE_RESOURCE_DIRECTORY *ret;
    WORD list[9];
    int i, pos = 0;

    if (flags & FILE_VER_GET_LOCALISED)
    {
        /* cf. LdrFindResource_U */
        pos = push_language( list, pos, MAKELANGID( LANG_NEUTRAL, SUBLANG_NEUTRAL ) );
        pos = push_language( list, pos, LANGIDFROMLCID( NtCurrentTeb()->CurrentLocale ) );
        pos = push_language( list, pos, GetUserDefaultLangID() );
        pos = push_language( list, pos, MAKELANGID( PRIMARYLANGID(GetUserDefaultLangID()), SUBLANG_NEUTRAL ));
        pos = push_language( list, pos, MAKELANGID( PRIMARYLANGID(GetUserDefaultLangID()), SUBLANG_DEFAULT ));
        pos = push_language( list, pos, GetSystemDefaultLangID() );
        pos = push_language( list, pos, MAKELANGID( PRIMARYLANGID(GetSystemDefaultLangID()), SUBLANG_NEUTRAL ));
        pos = push_language( list, pos, MAKELANGID( PRIMARYLANGID(GetSystemDefaultLangID()), SUBLANG_DEFAULT ));
        pos = push_language( list, pos, MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT ) );
    }
    else
    {
        /* FIXME: resolve LN file here */
        pos = push_language( list, pos, MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT ) );
    }

    for (i = 0; i < pos; i++) if ((ret = find_entry_by_id( dir, list[i], root, root_size ))) return ret;
    return find_entry_default( dir, root );
}


static DWORD read_data( HANDLE handle, DWORD offset, void *data, DWORD len )
{
    DWORD res;

    SetFilePointer( handle, offset, NULL, FILE_BEGIN );
    if (!ReadFile( handle, data, len, &res, NULL )) res = 0;
    return res;
}

/***********************************************************************
 *           find_ne_resource         [internal]
 */
static BOOL find_ne_resource( HANDLE handle, DWORD *resLen, DWORD *resOff )
{
    const WORD typeid = VS_FILE_INFO | 0x8000;
    const WORD resid = VS_VERSION_INFO | 0x8000;
    IMAGE_OS2_HEADER nehd;
    NE_TYPEINFO *typeInfo;
    NE_NAMEINFO *nameInfo;
    DWORD nehdoffset = *resOff;
    LPBYTE resTab;
    DWORD resTabSize;
    int count;

    /* Read in NE header */
    if (read_data( handle, nehdoffset, &nehd, sizeof(nehd) ) != sizeof(nehd)) return FALSE;

    resTabSize = nehd.ne_restab - nehd.ne_rsrctab;
    if ( !resTabSize )
    {
        TRACE("No resources in NE dll\n" );
        return FALSE;
    }

    /* Read in resource table */
    resTab = HeapAlloc( GetProcessHeap(), 0, resTabSize );
    if ( !resTab ) return FALSE;

    if (read_data( handle, nehd.ne_rsrctab + nehdoffset, resTab, resTabSize ) != resTabSize)
    {
        HeapFree( GetProcessHeap(), 0, resTab );
        return FALSE;
    }

    /* Find resource */
    typeInfo = (NE_TYPEINFO *)(resTab + 2);
    while (typeInfo->type_id)
    {
        if (typeInfo->type_id == typeid) goto found_type;
        typeInfo = (NE_TYPEINFO *)((char *)(typeInfo + 1) +
                                   typeInfo->count * sizeof(NE_NAMEINFO));
    }
    TRACE("No typeid entry found\n" );
    HeapFree( GetProcessHeap(), 0, resTab );
    return FALSE;

 found_type:
    nameInfo = (NE_NAMEINFO *)(typeInfo + 1);

    for (count = typeInfo->count; count > 0; count--, nameInfo++)
        if (nameInfo->id == resid) goto found_name;

    TRACE("No resid entry found\n" );
    HeapFree( GetProcessHeap(), 0, resTab );
    return FALSE;

 found_name:
    /* Return resource data */
    *resLen = nameInfo->length << *(WORD *)resTab;
    *resOff = nameInfo->offset << *(WORD *)resTab;

    HeapFree( GetProcessHeap(), 0, resTab );
    return TRUE;
}

/***********************************************************************
 *           find_pe_resource         [internal]
 */
static BOOL find_pe_resource( HANDLE handle, DWORD *resLen, DWORD *resOff, DWORD flags )
{
    union
    {
        IMAGE_NT_HEADERS32 nt32;
        IMAGE_NT_HEADERS64 nt64;
    } pehd;
    DWORD pehdoffset = *resOff;
    PIMAGE_DATA_DIRECTORY resDataDir;
    PIMAGE_SECTION_HEADER sections;
    LPBYTE resSection;
    DWORD len, section_size, data_size, resDirSize;
    const void *resDir;
    const IMAGE_RESOURCE_DIRECTORY *resPtr;
    const IMAGE_RESOURCE_DATA_ENTRY *resData;
    int i, nSections;
    BOOL ret = FALSE;

    /* Read in PE header */
    len = read_data( handle, pehdoffset, &pehd, sizeof(pehd) );
    if (len < sizeof(pehd.nt32.FileHeader)) return FALSE;
    if (len < sizeof(pehd)) memset( (char *)&pehd + len, 0, sizeof(pehd) - len );

    switch (pehd.nt32.OptionalHeader.Magic)
    {
    case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
        resDataDir = pehd.nt32.OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_RESOURCE;
        break;
    case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
        resDataDir = pehd.nt64.OptionalHeader.DataDirectory + IMAGE_DIRECTORY_ENTRY_RESOURCE;
        break;
    default:
        return FALSE;
    }

    if ( !resDataDir->Size )
    {
        TRACE("No resources in PE dll\n" );
        return FALSE;
    }

    /* Read in section table */
    nSections = pehd.nt32.FileHeader.NumberOfSections;
    sections = HeapAlloc( GetProcessHeap(), 0,
                          nSections * sizeof(IMAGE_SECTION_HEADER) );
    if ( !sections ) return FALSE;

    len = FIELD_OFFSET( IMAGE_NT_HEADERS32, OptionalHeader ) + pehd.nt32.FileHeader.SizeOfOptionalHeader;
    if (read_data( handle, pehdoffset + len, sections, nSections * sizeof(IMAGE_SECTION_HEADER) ) !=
        nSections * sizeof(IMAGE_SECTION_HEADER))
    {
        HeapFree( GetProcessHeap(), 0, sections );
        return FALSE;
    }

    /* Find resource section */
    for ( i = 0; i < nSections; i++ )
        if (    resDataDir->VirtualAddress >= sections[i].VirtualAddress
             && resDataDir->VirtualAddress <  sections[i].VirtualAddress +
                                              sections[i].SizeOfRawData )
            break;

    if ( i == nSections )
    {
        HeapFree( GetProcessHeap(), 0, sections );
        TRACE("Couldn't find resource section\n" );
        return FALSE;
    }

    /* Read in resource section */
    data_size = sections[i].SizeOfRawData;
    section_size = max( data_size, sections[i].Misc.VirtualSize );
    resSection = HeapAlloc( GetProcessHeap(), 0, section_size );
    if ( !resSection )
    {
        HeapFree( GetProcessHeap(), 0, sections );
        return FALSE;
    }

    if (read_data( handle, sections[i].PointerToRawData, resSection, data_size ) != data_size) goto done;
    if (data_size < section_size) memset( (char *)resSection + data_size, 0, section_size - data_size );

    /* Find resource */
    resDir = resSection + (resDataDir->VirtualAddress - sections[i].VirtualAddress);
    resDirSize = section_size - (resDataDir->VirtualAddress - sections[i].VirtualAddress);

    resPtr = resDir;
    resPtr = find_entry_by_id( resPtr, VS_FILE_INFO, resDir, resDirSize );
    if ( !resPtr )
    {
        TRACE("No typeid entry found\n" );
        goto done;
    }
    resPtr = find_entry_by_id( resPtr, VS_VERSION_INFO, resDir, resDirSize );
    if ( !resPtr )
    {
        TRACE("No resid entry found\n" );
        goto done;
    }
    resPtr = find_entry_language( resPtr, resDir, resDirSize, flags );
    if ( !resPtr )
    {
        TRACE("No default language entry found\n" );
        goto done;
    }

    /* Find resource data section */
    resData = (const IMAGE_RESOURCE_DATA_ENTRY*)resPtr;
    for ( i = 0; i < nSections; i++ )
        if (    resData->OffsetToData >= sections[i].VirtualAddress
             && resData->OffsetToData <  sections[i].VirtualAddress +
                                         sections[i].SizeOfRawData )
            break;

    if ( i == nSections )
    {
        TRACE("Couldn't find resource data section\n" );
        goto done;
    }

    /* Return resource data */
    *resLen = resData->Size;
    *resOff = resData->OffsetToData - sections[i].VirtualAddress + sections[i].PointerToRawData;
    ret = TRUE;

 done:
    HeapFree( GetProcessHeap(), 0, resSection );
    HeapFree( GetProcessHeap(), 0, sections );
    return ret;
}


/***********************************************************************
 *           find_version_resource         [internal]
 */
static DWORD find_version_resource( HANDLE handle, DWORD *reslen, DWORD *offset, DWORD flags )
{
    IMAGE_DOS_HEADER mzh;
    WORD magic;

    if (read_data( handle, 0, &mzh, sizeof(mzh) ) != sizeof(mzh)) return 0;
    if (mzh.e_magic != IMAGE_DOS_SIGNATURE) return 0;

    if (read_data( handle, mzh.e_lfanew, &magic, sizeof(magic) ) != sizeof(magic)) return 0;
    *offset = mzh.e_lfanew;

    switch (magic)
    {
    case IMAGE_OS2_SIGNATURE:
        if (!find_ne_resource( handle, reslen, offset )) magic = 0;
        break;
    case IMAGE_NT_SIGNATURE:
        if (!find_pe_resource( handle, reslen, offset, flags )) magic = 0;
        break;
    }
    WARN( "Can't handle %04x files.\n", magic );
    return magic;
}

/******************************************************************************
 *   This function will print via standard TRACE, debug info regarding
 *   the file info structure vffi.
 */
static void print_vffi_debug(const VS_FIXEDFILEINFO *vffi)
{
    BOOL    versioned_printer = FALSE;

    if((vffi->dwFileType == VFT_DLL) || (vffi->dwFileType == VFT_DRV))
    {
        if(vffi->dwFileSubtype == VFT2_DRV_VERSIONED_PRINTER)
            /* this is documented for newer w2k Drivers and up */
            versioned_printer = TRUE;
        else if( (vffi->dwFileSubtype == VFT2_DRV_PRINTER) &&
                 (vffi->dwFileVersionMS != vffi->dwProductVersionMS) &&
                 (vffi->dwFileVersionMS > 0) &&
                 (vffi->dwFileVersionMS <= 3) )
            /* found this on NT 3.51, NT4.0 and old w2k Drivers */
            versioned_printer = TRUE;
    }

    TRACE("structversion=%u.%u, ",
            HIWORD(vffi->dwStrucVersion),LOWORD(vffi->dwStrucVersion));
    if(versioned_printer)
    {
        WORD mode = LOWORD(vffi->dwFileVersionMS);
        WORD ver_rev = HIWORD(vffi->dwFileVersionLS);
        TRACE("fileversion=%lu.%u.%u.%u (%s.major.minor.release), ",
            (vffi->dwFileVersionMS),
            HIBYTE(ver_rev), LOBYTE(ver_rev), LOWORD(vffi->dwFileVersionLS),
            (mode == 3) ? "Usermode" : ((mode <= 2) ? "Kernelmode" : "?") );
    }
    else
    {
        TRACE("fileversion=%u.%u.%u.%u, ",
            HIWORD(vffi->dwFileVersionMS),LOWORD(vffi->dwFileVersionMS),
            HIWORD(vffi->dwFileVersionLS),LOWORD(vffi->dwFileVersionLS));
    }
    TRACE("productversion=%u.%u.%u.%u\n",
          HIWORD(vffi->dwProductVersionMS),LOWORD(vffi->dwProductVersionMS),
          HIWORD(vffi->dwProductVersionLS),LOWORD(vffi->dwProductVersionLS));

    TRACE("flagmask=0x%lx, flags=0x%lx %s%s%s%s%s%s\n",
          vffi->dwFileFlagsMask, vffi->dwFileFlags,
          (vffi->dwFileFlags & VS_FF_DEBUG) ? "DEBUG," : "",
          (vffi->dwFileFlags & VS_FF_PRERELEASE) ? "PRERELEASE," : "",
          (vffi->dwFileFlags & VS_FF_PATCHED) ? "PATCHED," : "",
          (vffi->dwFileFlags & VS_FF_PRIVATEBUILD) ? "PRIVATEBUILD," : "",
          (vffi->dwFileFlags & VS_FF_INFOINFERRED) ? "INFOINFERRED," : "",
          (vffi->dwFileFlags & VS_FF_SPECIALBUILD) ? "SPECIALBUILD," : "");

    TRACE("(");

    TRACE("OS=0x%x.0x%x ", HIWORD(vffi->dwFileOS), LOWORD(vffi->dwFileOS));

    switch (vffi->dwFileOS&0xFFFF0000)
    {
    case VOS_DOS:TRACE("DOS,");break;
    case VOS_OS216:TRACE("OS/2-16,");break;
    case VOS_OS232:TRACE("OS/2-32,");break;
    case VOS_NT:TRACE("NT,");break;
    case VOS_UNKNOWN:
    default:
        TRACE("UNKNOWN(0x%lx),",vffi->dwFileOS&0xFFFF0000);break;
    }

    switch (LOWORD(vffi->dwFileOS))
    {
    case VOS__BASE:TRACE("BASE");break;
    case VOS__WINDOWS16:TRACE("WIN16");break;
    case VOS__WINDOWS32:TRACE("WIN32");break;
    case VOS__PM16:TRACE("PM16");break;
    case VOS__PM32:TRACE("PM32");break;
    default:
        TRACE("UNKNOWN(0x%x)",LOWORD(vffi->dwFileOS));break;
    }

    TRACE(")\n");

    switch (vffi->dwFileType)
    {
    case VFT_APP:TRACE("filetype=APP");break;
    case VFT_DLL:
        TRACE("filetype=DLL");
        if(vffi->dwFileSubtype != 0)
        {
            if(versioned_printer) /* NT3.x/NT4.0 or old w2k Driver  */
                TRACE(",PRINTER");
            TRACE(" (subtype=0x%lx)", vffi->dwFileSubtype);
        }
        break;
    case VFT_DRV:
        TRACE("filetype=DRV,");
        switch(vffi->dwFileSubtype)
        {
        case VFT2_DRV_PRINTER:TRACE("PRINTER");break;
        case VFT2_DRV_KEYBOARD:TRACE("KEYBOARD");break;
        case VFT2_DRV_LANGUAGE:TRACE("LANGUAGE");break;
        case VFT2_DRV_DISPLAY:TRACE("DISPLAY");break;
        case VFT2_DRV_MOUSE:TRACE("MOUSE");break;
        case VFT2_DRV_NETWORK:TRACE("NETWORK");break;
        case VFT2_DRV_SYSTEM:TRACE("SYSTEM");break;
        case VFT2_DRV_INSTALLABLE:TRACE("INSTALLABLE");break;
        case VFT2_DRV_SOUND:TRACE("SOUND");break;
        case VFT2_DRV_COMM:TRACE("COMM");break;
        case VFT2_DRV_INPUTMETHOD:TRACE("INPUTMETHOD");break;
        case VFT2_DRV_VERSIONED_PRINTER:TRACE("VERSIONED_PRINTER");break;
        case VFT2_UNKNOWN:
        default:
            TRACE("UNKNOWN(0x%lx)",vffi->dwFileSubtype);break;
        }
        break;
    case VFT_FONT:
        TRACE("filetype=FONT,");
        switch (vffi->dwFileSubtype)
        {
        case VFT2_FONT_RASTER:TRACE("RASTER");break;
        case VFT2_FONT_VECTOR:TRACE("VECTOR");break;
        case VFT2_FONT_TRUETYPE:TRACE("TRUETYPE");break;
        default:TRACE("UNKNOWN(0x%lx)",vffi->dwFileSubtype);break;
        }
        break;
    case VFT_VXD:TRACE("filetype=VXD");break;
    case VFT_STATIC_LIB:TRACE("filetype=STATIC_LIB");break;
    case VFT_UNKNOWN:
    default:
        TRACE("filetype=Unknown(0x%lx)",vffi->dwFileType);break;
    }

    TRACE("\n");
    TRACE("filedate=0x%lx.0x%lx\n",vffi->dwFileDateMS,vffi->dwFileDateLS);
}

/***********************************************************************
 *           GetFileVersionInfoSizeW         (kernelbase.@)
 */
DWORD WINAPI GetFileVersionInfoSizeW( LPCWSTR filename, LPDWORD handle )
{
    return GetFileVersionInfoSizeExW( FILE_VER_GET_LOCALISED, filename, handle );
}

/***********************************************************************
 *           GetFileVersionInfoSizeA         (kernelbase.@)
 */
DWORD WINAPI GetFileVersionInfoSizeA( LPCSTR filename, LPDWORD handle )
{
    return GetFileVersionInfoSizeExA( FILE_VER_GET_LOCALISED, filename, handle );
}

/******************************************************************************
 *           GetFileVersionInfoSizeExW       (kernelbase.@)
 */
DWORD WINAPI GetFileVersionInfoSizeExW( DWORD flags, LPCWSTR filename, LPDWORD ret_handle )
{
    DWORD len, offset, magic = 1;
    HMODULE hModule;

    TRACE("(0x%lx,%s,%p)\n", flags, debugstr_w(filename), ret_handle );

    if (ret_handle) *ret_handle = 0;

    if (!filename)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!*filename)
    {
        SetLastError(ERROR_BAD_PATHNAME);
        return 0;
    }
    if (flags & ~FILE_VER_GET_LOCALISED)
        FIXME("flags 0x%lx ignored\n", flags & ~FILE_VER_GET_LOCALISED);

    if ((hModule = LoadLibraryExW( filename, 0, LOAD_LIBRARY_AS_IMAGE_RESOURCE )))
    {
        HRSRC hRsrc = NULL;
        if (!(flags & FILE_VER_GET_LOCALISED))
        {
            LANGID english = MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT );
            hRsrc = FindResourceExW( hModule, (LPWSTR)VS_FILE_INFO,
                                     MAKEINTRESOURCEW(VS_VERSION_INFO), english );
        }
        if (!hRsrc)
            hRsrc = FindResourceW( hModule, MAKEINTRESOURCEW(VS_VERSION_INFO),
                                   (LPWSTR)VS_FILE_INFO );
        if (hRsrc)
        {
            magic = IMAGE_NT_SIGNATURE;
            len = SizeofResource( hModule, hRsrc );
        }
        FreeLibrary( hModule );
    }
    else
    {
        HANDLE handle = CreateFileW( filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING, 0, 0 );
        if (handle == INVALID_HANDLE_VALUE) return 0;
        magic = find_version_resource( handle, &len, &offset, flags );
        CloseHandle( handle );
    }

    switch (magic)
    {
    case IMAGE_OS2_SIGNATURE:
        /* We have a 16bit resource.
         *
         * XP/W2K/W2K3 uses a buffer which is more than the actual needed space:
         *
         * (info->wLength - sizeof(VS_FIXEDFILEINFO)) * 4
         *
         * This extra buffer is used for ANSI to Unicode conversions in W-Calls.
         * info->wLength should be the same as len. Currently it isn't but that
         * doesn't seem to be a problem (len is bigger than info->wLength).
         */
        SetLastError(0);
        return (len - sizeof(VS_FIXEDFILEINFO)) * 4;

    case IMAGE_NT_SIGNATURE:
        /* We have a 32bit resource.
         *
         * XP/W2K/W2K3 uses a buffer which is 2 times the actual needed space + 4 bytes "FE2X"
         * This extra buffer is used for Unicode to ANSI conversions in A-Calls
         */
        SetLastError(0);
        return (len * 2) + 4;

    default:
        if (GetVersion() & 0x80000000) /* Windows 95/98 */
            SetLastError(ERROR_FILE_NOT_FOUND);
        else
            SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
        return 0;
    }
}

/******************************************************************************
 *           GetFileVersionInfoSizeExA       (kernelbase.@)
 */
DWORD WINAPI GetFileVersionInfoSizeExA( DWORD flags, LPCSTR filename, LPDWORD handle )
{
    UNICODE_STRING filenameW;
    DWORD retval;

    TRACE("(0x%lx,%s,%p)\n", flags, debugstr_a(filename), handle );

    if(filename)
        RtlCreateUnicodeStringFromAsciiz(&filenameW, filename);
    else
        filenameW.Buffer = NULL;

    retval = GetFileVersionInfoSizeExW(flags, filenameW.Buffer, handle);

    RtlFreeUnicodeString(&filenameW);

    return retval;
}

/***********************************************************************
 *           GetFileVersionInfoExW           (kernelbase.@)
 */
BOOL WINAPI GetFileVersionInfoExW( DWORD flags, LPCWSTR filename, DWORD ignored, DWORD datasize, LPVOID data )
{
    static const char signature[4] = "FE2X";
    DWORD len, offset, magic = 1;
    HMODULE hModule;
    VS_VERSION_INFO_STRUCT32* vvis = data;

    TRACE("(0x%lx,%s,%ld,size=%ld,data=%p)\n",
          flags, debugstr_w(filename), ignored, datasize, data );

    if (!data)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (flags & ~FILE_VER_GET_LOCALISED)
        FIXME("flags 0x%lx ignored\n", flags & ~FILE_VER_GET_LOCALISED);

    if ((hModule = LoadLibraryExW( filename, 0, LOAD_LIBRARY_AS_IMAGE_RESOURCE )))
    {
        HRSRC hRsrc = NULL;
        if (!(flags & FILE_VER_GET_LOCALISED))
        {
            LANGID english = MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT );
            hRsrc = FindResourceExW( hModule, (LPWSTR)VS_FILE_INFO,
                                     MAKEINTRESOURCEW(VS_VERSION_INFO), english );
        }
        if (!hRsrc)
            hRsrc = FindResourceW( hModule, MAKEINTRESOURCEW(VS_VERSION_INFO),
                                   (LPWSTR)VS_FILE_INFO );
        if (hRsrc)
        {
            HGLOBAL hMem = LoadResource( hModule, hRsrc );
            magic = IMAGE_NT_SIGNATURE;
            len = min( SizeofResource(hModule, hRsrc), datasize );
            memcpy( data, LockResource( hMem ), len );
            FreeResource( hMem );
        }
        FreeLibrary( hModule );
    }
    else
    {
        HANDLE handle = CreateFileW( filename, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING, 0, 0 );
        if (handle == INVALID_HANDLE_VALUE) return 0;
        if ((magic = find_version_resource( handle, &len, &offset, flags )))
            len = read_data( handle, offset, data, min( len, datasize ));
        CloseHandle( handle );
    }

    switch (magic)
    {
    case IMAGE_OS2_SIGNATURE:
        /* We have a 16bit resource. */
        if (TRACE_ON(ver))
            print_vffi_debug( (VS_FIXEDFILEINFO *)VersionInfo16_Value( (VS_VERSION_INFO_STRUCT16 *)data ));
        SetLastError(0);
        return TRUE;

    case IMAGE_NT_SIGNATURE:
        /* We have a 32bit resource.
         *
         * XP/W2K/W2K3 uses a buffer which is 2 times the actual needed space + 4 bytes "FE2X"
         * This extra buffer is used for Unicode to ANSI conversions in A-Calls
         */
        len = vvis->wLength + sizeof(signature);
        if (datasize >= len) memcpy( (char*)data + vvis->wLength, signature, sizeof(signature) );
        if (TRACE_ON(ver))
            print_vffi_debug( (VS_FIXEDFILEINFO *)VersionInfo32_Value( vvis ));
        SetLastError(0);
        return TRUE;

    default:
        SetLastError( ERROR_RESOURCE_DATA_NOT_FOUND );
        return FALSE;
    }
}

/***********************************************************************
 *           GetFileVersionInfoExA           (kernelbase.@)
 */
BOOL WINAPI GetFileVersionInfoExA( DWORD flags, LPCSTR filename, DWORD handle, DWORD datasize, LPVOID data )
{
    UNICODE_STRING filenameW;
    BOOL retval;

    TRACE("(0x%lx,%s,%ld,size=%ld,data=%p)\n",
          flags, debugstr_a(filename), handle, datasize, data );

    if(filename)
        RtlCreateUnicodeStringFromAsciiz(&filenameW, filename);
    else
        filenameW.Buffer = NULL;

    retval = GetFileVersionInfoExW(flags, filenameW.Buffer, handle, datasize, data);

    RtlFreeUnicodeString(&filenameW);

    return retval;
}

/***********************************************************************
 *           GetFileVersionInfoW             (kernelbase.@)
 */
BOOL WINAPI GetFileVersionInfoW( LPCWSTR filename, DWORD handle, DWORD datasize, LPVOID data )
{
    return GetFileVersionInfoExW(FILE_VER_GET_LOCALISED, filename, handle, datasize, data);
}

/***********************************************************************
 *           GetFileVersionInfoA             (kernelbase.@)
 */
BOOL WINAPI GetFileVersionInfoA( LPCSTR filename, DWORD handle, DWORD datasize, LPVOID data )
{
    return GetFileVersionInfoExA(FILE_VER_GET_LOCALISED, filename, handle, datasize, data);
}

/***********************************************************************
 *           VersionInfo16_FindChild             [internal]
 */
static const VS_VERSION_INFO_STRUCT16 *VersionInfo16_FindChild( const VS_VERSION_INFO_STRUCT16 *info,
                                                                LPCSTR key, UINT len )
{
    const VS_VERSION_INFO_STRUCT16 *child = VersionInfo16_Children( info );

    while ((char *)child < (char *)info + info->wLength )
    {
        if (!strnicmp( child->szKey, key, len ) && !child->szKey[len])
            return child;

        if (!(child->wLength)) return NULL;
        child = VersionInfo16_Next( child );
    }

    return NULL;
}

/***********************************************************************
 *           VersionInfo32_FindChild             [internal]
 */
static const VS_VERSION_INFO_STRUCT32 *VersionInfo32_FindChild( const VS_VERSION_INFO_STRUCT32 *info,
                                                                LPCWSTR key, UINT len )
{
    const VS_VERSION_INFO_STRUCT32 *child = VersionInfo32_Children( info );

    while ((char *)child < (char *)info + info->wLength )
    {
        if (!wcsnicmp( child->szKey, key, len ) && !child->szKey[len])
            return child;

        if (!(child->wLength)) return NULL;
        child = VersionInfo32_Next( child );
    }

    return NULL;
}

/***********************************************************************
 *           VersionInfo16_QueryValue              [internal]
 *
 *    Gets a value from a 16-bit NE resource
 */
static BOOL VersionInfo16_QueryValue( const VS_VERSION_INFO_STRUCT16 *info, LPCSTR lpSubBlock,
                               LPVOID *lplpBuffer, UINT *puLen )
{
    while ( *lpSubBlock )
    {
        /* Find next path component */
        LPCSTR lpNextSlash;
        for ( lpNextSlash = lpSubBlock; *lpNextSlash; lpNextSlash++ )
            if ( *lpNextSlash == '\\' )
                break;

        /* Skip empty components */
        if ( lpNextSlash == lpSubBlock )
        {
            lpSubBlock++;
            continue;
        }

        /* We have a non-empty component: search info for key */
        info = VersionInfo16_FindChild( info, lpSubBlock, lpNextSlash-lpSubBlock );
        if ( !info )
        {
            if (puLen) *puLen = 0 ;
            SetLastError( ERROR_RESOURCE_TYPE_NOT_FOUND );
            return FALSE;
        }

        /* Skip path component */
        lpSubBlock = lpNextSlash;
    }

    /* Return value */
    *lplpBuffer = VersionInfo16_Value( info );
    if (puLen)
        *puLen = info->wValueLength;

    return TRUE;
}

/***********************************************************************
 *           VersionInfo32_QueryValue              [internal]
 *
 *    Gets a value from a 32-bit PE resource
 */
static BOOL VersionInfo32_QueryValue( const VS_VERSION_INFO_STRUCT32 *info, LPCWSTR lpSubBlock,
                                      LPVOID *lplpBuffer, UINT *puLen, BOOL *pbText )
{
    PVOID ptr;
    TRACE("lpSubBlock : (%s)\n", debugstr_w(lpSubBlock));

    while ( *lpSubBlock )
    {
        /* Find next path component */
        LPCWSTR lpNextSlash;
        for ( lpNextSlash = lpSubBlock; *lpNextSlash; lpNextSlash++ )
            if ( *lpNextSlash == '\\' )
                break;

        /* Skip empty components */
        if ( lpNextSlash == lpSubBlock )
        {
            lpSubBlock++;
            continue;
        }

        /* We have a non-empty component: search info for key */
        info = VersionInfo32_FindChild( info, lpSubBlock, lpNextSlash-lpSubBlock );
        if ( !info )
        {
            if (puLen) *puLen = 0 ;
            SetLastError( ERROR_RESOURCE_TYPE_NOT_FOUND );
            return FALSE;
        }

        /* Skip path component */
        lpSubBlock = lpNextSlash;
    }

    /* Return value */
    ptr = VersionInfo32_Value(info);
    if ((PBYTE)ptr >= ((PBYTE)info + info->wLength))  /* empty value */
        ptr = (WCHAR*)info->szKey + wcslen(info->szKey);

    *lplpBuffer = ptr;
    if (puLen)
        *puLen = info->wValueLength;
    if (pbText)
        *pbText = info->wType;

    return TRUE;
}

/***********************************************************************
 *           VerQueryValueA              (kernelbase.@)
 */
BOOL WINAPI VerQueryValueA( LPCVOID pBlock, LPCSTR lpSubBlock,
                               LPVOID *lplpBuffer, PUINT puLen )
{
    static const char rootA[] = "\\";
    const VS_VERSION_INFO_STRUCT16 *info = pBlock;

    TRACE("(%p,%s,%p,%p)\n",
                pBlock, debugstr_a(lpSubBlock), lplpBuffer, puLen );

     if (!pBlock)
        return FALSE;

    if (lpSubBlock == NULL || lpSubBlock[0] == '\0')
        lpSubBlock = rootA;

    if ( !VersionInfoIs16( info ) )
    {
        BOOL ret, isText;
        INT len;
        LPWSTR lpSubBlockW;
        UINT value_len;

        len  = MultiByteToWideChar(CP_ACP, 0, lpSubBlock, -1, NULL, 0);
        lpSubBlockW = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

        if (!lpSubBlockW)
            return FALSE;

        MultiByteToWideChar(CP_ACP, 0, lpSubBlock, -1, lpSubBlockW, len);

        ret = VersionInfo32_QueryValue(pBlock, lpSubBlockW, lplpBuffer, &value_len, &isText);
        if (puLen) *puLen = value_len;

        HeapFree(GetProcessHeap(), 0, lpSubBlockW);

        if (ret && isText)
        {
            /* Set lpBuffer so it points to the 'empty' area where we store
             * the converted strings
             */
            LPSTR lpBufferA = (LPSTR)pBlock + info->wLength + 4;
            DWORD pos = (LPCSTR)*lplpBuffer - (LPCSTR)pBlock;
            len = WideCharToMultiByte(CP_ACP, 0, *lplpBuffer, value_len,
                                      lpBufferA + pos, info->wLength - pos, NULL, NULL);
            *lplpBuffer = lpBufferA + pos;
            if (puLen) *puLen = len;
        }
        return ret;
    }

    return VersionInfo16_QueryValue(info, lpSubBlock, lplpBuffer, puLen);
}

/***********************************************************************
 *           VerQueryValueW              (kernelbase.@)
 */
BOOL WINAPI VerQueryValueW( LPCVOID pBlock, LPCWSTR lpSubBlock,
                               LPVOID *lplpBuffer, PUINT puLen )
{
    const VS_VERSION_INFO_STRUCT32 *info = pBlock;

    TRACE("(%p,%s,%p,%p)\n",
                pBlock, debugstr_w(lpSubBlock), lplpBuffer, puLen );

    if (!pBlock)
        return FALSE;

    if (!lpSubBlock || !lpSubBlock[0])
        lpSubBlock = L"\\";

    if ( VersionInfoIs16( info ) )
    {
        BOOL ret;
        int len;
        LPSTR lpSubBlockA;

        len = WideCharToMultiByte(CP_ACP, 0, lpSubBlock, -1, NULL, 0, NULL, NULL);
        lpSubBlockA = HeapAlloc(GetProcessHeap(), 0, len * sizeof(char));

        if (!lpSubBlockA)
            return FALSE;

        WideCharToMultiByte(CP_ACP, 0, lpSubBlock, -1, lpSubBlockA, len, NULL, NULL);

        ret = VersionInfo16_QueryValue(pBlock, lpSubBlockA, lplpBuffer, puLen);

        HeapFree(GetProcessHeap(), 0, lpSubBlockA);

        if (ret && wcscmp( lpSubBlock, L"\\" ) && wcsicmp( lpSubBlock, L"\\VarFileInfo\\Translation" ))
        {
            /* Set lpBuffer so it points to the 'empty' area where we store
             * the converted strings
             */
            LPWSTR lpBufferW = (LPWSTR)((LPSTR)pBlock + info->wLength);
            DWORD pos = (LPCSTR)*lplpBuffer - (LPCSTR)pBlock;
            DWORD max = (info->wLength - sizeof(VS_FIXEDFILEINFO)) * 4 - info->wLength;

            len = MultiByteToWideChar(CP_ACP, 0, *lplpBuffer, -1,
                                      lpBufferW + pos, max/sizeof(WCHAR) - pos );
            *lplpBuffer = lpBufferW + pos;
            if (puLen) *puLen = len;
        }
        return ret;
    }

    return VersionInfo32_QueryValue(info, lpSubBlock, lplpBuffer, puLen, NULL);
}


/******************************************************************************
 *   file_existsA
 */
static BOOL file_existsA( char const * path, char const * file, BOOL excl )
{
    DWORD sharing = excl ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE;
    char filename[MAX_PATH];
    int len;
    HANDLE handle;

    if (path)
    {
        strcpy( filename, path );
        len = strlen(filename);
        if (len && filename[len - 1] != '\\') strcat( filename, "\\" );
        strcat( filename, file );
    }
    else if (!SearchPathA( NULL, file, NULL, MAX_PATH, filename, NULL )) return FALSE;

    handle = CreateFileA( filename, 0, sharing, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0 );
    if (handle == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle( handle );
    return TRUE;
}

/******************************************************************************
 *   file_existsW
 */
static BOOL file_existsW( const WCHAR *path, const WCHAR *file, BOOL excl )
{
    DWORD sharing = excl ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE;
    WCHAR filename[MAX_PATH];
    int len;
    HANDLE handle;

    if (path)
    {
        lstrcpyW( filename, path );
        len = lstrlenW(filename);
        if (len && filename[len - 1] != '\\') lstrcatW( filename, L"\\" );
        lstrcatW( filename, file );
    }
    else if (!SearchPathW( NULL, file, NULL, MAX_PATH, filename, NULL )) return FALSE;

    handle = CreateFileW( filename, 0, sharing, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0 );
    if (handle == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle( handle );
    return TRUE;
}

/*****************************************************************************
 *   VerFindFileA (kernelbase.@)
 *
 *   Determines where to install a file based on whether it locates another
 *   version of the file in the system.  The values VerFindFile returns are
 *   used in a subsequent call to the VerInstallFile function.
 */
DWORD WINAPI VerFindFileA( DWORD flags, LPCSTR filename, LPCSTR win_dir, LPCSTR app_dir,
                           LPSTR cur_dir, PUINT curdir_len, LPSTR dest, PUINT dest_len )
{
    DWORD  retval = 0;
    const char *curDir;
    const char *destDir;
    char winDir[MAX_PATH], systemDir[MAX_PATH];

    TRACE("flags = %lx filename=%s windir=%s appdir=%s curdirlen=%p(%u) destdirlen=%p(%u)\n",
          flags, debugstr_a(filename), debugstr_a(win_dir), debugstr_a(app_dir),
          curdir_len, curdir_len ? *curdir_len : 0, dest_len, dest_len ? *dest_len : 0 );

    /* Figure out where the file should go; shared files default to the
       system directory */

    GetSystemDirectoryA(systemDir, sizeof(systemDir));
    curDir = "";

    if(flags & VFFF_ISSHAREDFILE)
    {
        destDir = systemDir;
        /* Were we given a filename?  If so, try to find the file. */
        if(filename)
        {
            if(file_existsA(destDir, filename, FALSE)) curDir = destDir;
            else if(app_dir && file_existsA(app_dir, filename, FALSE))
                curDir = app_dir;

            if(!file_existsA(systemDir, filename, FALSE))
                retval |= VFF_CURNEDEST;
        }
    }
    else /* not a shared file */
    {
        destDir = app_dir ? app_dir : "";
        if(filename)
        {
            GetWindowsDirectoryA( winDir, MAX_PATH );
            if(file_existsA(destDir, filename, FALSE)) curDir = destDir;
            else if(file_existsA(winDir, filename, FALSE))
                curDir = winDir;
            else if(file_existsA(systemDir, filename, FALSE))
                curDir = systemDir;

            if (app_dir && app_dir[0])
            {
                if(!file_existsA(app_dir, filename, FALSE))
                    retval |= VFF_CURNEDEST;
            }
            else if(file_existsA(NULL, filename, FALSE))
                retval |= VFF_CURNEDEST;
        }
    }

    /* Check to see if the file exists and is in use by another application */
    if (filename && file_existsA(curDir, filename, FALSE))
    {
        if (filename && !file_existsA(curDir, filename, TRUE))
           retval |= VFF_FILEINUSE;
    }

    if (dest_len && dest)
    {
        UINT len = strlen(destDir) + 1;
        if (*dest_len < len) retval |= VFF_BUFFTOOSMALL;
        lstrcpynA(dest, destDir, *dest_len);
        *dest_len = len;
    }
    if (curdir_len && cur_dir)
    {
        UINT len = strlen(curDir) + 1;
        if (*curdir_len < len) retval |= VFF_BUFFTOOSMALL;
        lstrcpynA(cur_dir, curDir, *curdir_len);
        *curdir_len = len;
    }

    TRACE("ret = %lu (%s%s%s) curdir=%s destdir=%s\n", retval,
          (retval & VFF_CURNEDEST) ? "VFF_CURNEDEST " : "",
          (retval & VFF_FILEINUSE) ? "VFF_FILEINUSE " : "",
          (retval & VFF_BUFFTOOSMALL) ? "VFF_BUFFTOOSMALL " : "",
          debugstr_a(cur_dir), debugstr_a(dest));

    return retval;
}

/*****************************************************************************
 * VerFindFileW (kernelbase.@)
 */
DWORD WINAPI VerFindFileW( DWORD flags, LPCWSTR filename, LPCWSTR win_dir, LPCWSTR app_dir,
                           LPWSTR cur_dir, PUINT curdir_len, LPWSTR dest, PUINT dest_len )
{
    DWORD retval = 0;
    const WCHAR *curDir;
    const WCHAR *destDir;

    TRACE("flags = %lx filename=%s windir=%s appdir=%s curdirlen=%p(%u) destdirlen=%p(%u)\n",
          flags, debugstr_w(filename), debugstr_w(win_dir), debugstr_w(app_dir),
          curdir_len, curdir_len ? *curdir_len : 0, dest_len, dest_len ? *dest_len : 0 );

    /* Figure out where the file should go; shared files default to the
       system directory */

    curDir = L"";

    if(flags & VFFF_ISSHAREDFILE)
    {
        destDir = system_dir;
        /* Were we given a filename?  If so, try to find the file. */
        if(filename)
        {
            if(file_existsW(destDir, filename, FALSE)) curDir = destDir;
            else if(app_dir && file_existsW(app_dir, filename, FALSE))
            {
                curDir = app_dir;
                retval |= VFF_CURNEDEST;
            }
        }
    }
    else /* not a shared file */
    {
        destDir = app_dir ? app_dir : L"";
        if(filename)
        {
            if(file_existsW(destDir, filename, FALSE)) curDir = destDir;
            else if(file_existsW(windows_dir, filename, FALSE))
            {
                curDir = windows_dir;
                retval |= VFF_CURNEDEST;
            }
            else if (file_existsW(system_dir, filename, FALSE))
            {
                curDir = system_dir;
                retval |= VFF_CURNEDEST;
            }
        }
    }

    if (filename && !file_existsW(curDir, filename, TRUE))
        retval |= VFF_FILEINUSE;

    if (dest_len && dest)
    {
        UINT len = lstrlenW(destDir) + 1;
        if (*dest_len < len) retval |= VFF_BUFFTOOSMALL;
        lstrcpynW(dest, destDir, *dest_len);
        *dest_len = len;
    }
    if (curdir_len && cur_dir)
    {
        UINT len = lstrlenW(curDir) + 1;
        if (*curdir_len < len) retval |= VFF_BUFFTOOSMALL;
        lstrcpynW(cur_dir, curDir, *curdir_len);
        *curdir_len = len;
    }

    TRACE("ret = %lu (%s%s%s) curdir=%s destdir=%s\n", retval,
          (retval & VFF_CURNEDEST) ? "VFF_CURNEDEST " : "",
          (retval & VFF_FILEINUSE) ? "VFF_FILEINUSE " : "",
          (retval & VFF_BUFFTOOSMALL) ? "VFF_BUFFTOOSMALL " : "",
          debugstr_w(cur_dir), debugstr_w(dest));
    return retval;
}


/***********************************************************************
 *         GetProductInfo   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetProductInfo( DWORD os_major, DWORD os_minor,
                                              DWORD sp_major, DWORD sp_minor, DWORD *type )
{
    return RtlGetProductInfo( os_major, os_minor, sp_major, sp_minor, type );
}


/***********************************************************************
 *         GetVersion   (kernelbase.@)
 */
DWORD WINAPI GetVersion(void)
{
    OSVERSIONINFOEXW info;
    DWORD result;

    info.dwOSVersionInfoSize = sizeof(info);
    if (!GetVersionExW( (OSVERSIONINFOW *)&info )) return 0;

    result = MAKELONG( MAKEWORD( info.dwMajorVersion, info.dwMinorVersion ),
                       (info.dwPlatformId ^ 2) << 14 );

    if (info.dwPlatformId == VER_PLATFORM_WIN32_NT)
        result |= LOWORD(info.dwBuildNumber) << 16;
    return result;
}


/***********************************************************************
 *         GetVersionExA   (kernelbase.@)
 */
BOOL WINAPI GetVersionExA( OSVERSIONINFOA *info )
{
    OSVERSIONINFOEXW infoW;

    if (info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA) &&
        info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXA))
    {
        WARN( "wrong OSVERSIONINFO size from app (got: %ld)\n", info->dwOSVersionInfoSize );
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        return FALSE;
    }

    infoW.dwOSVersionInfoSize = sizeof(infoW);
    if (!GetVersionExW( (OSVERSIONINFOW *)&infoW )) return FALSE;

    info->dwMajorVersion = infoW.dwMajorVersion;
    info->dwMinorVersion = infoW.dwMinorVersion;
    info->dwBuildNumber  = infoW.dwBuildNumber;
    info->dwPlatformId   = infoW.dwPlatformId;
    WideCharToMultiByte( CP_ACP, 0, infoW.szCSDVersion, -1,
                         info->szCSDVersion, sizeof(info->szCSDVersion), NULL, NULL );

    if (info->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXA))
    {
        OSVERSIONINFOEXA *vex = (OSVERSIONINFOEXA *)info;
        vex->wServicePackMajor = infoW.wServicePackMajor;
        vex->wServicePackMinor = infoW.wServicePackMinor;
        vex->wSuiteMask        = infoW.wSuiteMask;
        vex->wProductType      = infoW.wProductType;
    }
    return TRUE;
}


/***********************************************************************
 *         GetVersionExW   (kernelbase.@)
 */
BOOL WINAPI GetVersionExW( OSVERSIONINFOW *info )
{
    static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

    if (info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOW) &&
        info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXW))
    {
        WARN( "wrong OSVERSIONINFO size from app (got: %ld)\n", info->dwOSVersionInfoSize );
        return FALSE;
    }

    if (!InitOnceExecuteOnce(&init_once, init_current_version, NULL, NULL)) return FALSE;

    info->dwMajorVersion = current_version.dwMajorVersion;
    info->dwMinorVersion = current_version.dwMinorVersion;
    info->dwBuildNumber  = current_version.dwBuildNumber;
    info->dwPlatformId   = current_version.dwPlatformId;
    wcscpy( info->szCSDVersion, current_version.szCSDVersion );

    if (info->dwOSVersionInfoSize == sizeof(OSVERSIONINFOEXW))
    {
        OSVERSIONINFOEXW *vex = (OSVERSIONINFOEXW *)info;
        vex->wServicePackMajor = current_version.wServicePackMajor;
        vex->wServicePackMinor = current_version.wServicePackMinor;
        vex->wSuiteMask        = current_version.wSuiteMask;
        vex->wProductType      = current_version.wProductType;
    }
    return TRUE;
}

static LONG package_str_from_env( const WCHAR *var, UINT32 *length, WCHAR *name );
static LONG package_fill_str( const WCHAR *src, UINT32 *length, WCHAR *out );
static BOOL get_current_package_full_name( WCHAR *buf, UINT32 cch );
LONG WINAPI GetCurrentPackagePath( UINT32 *length, WCHAR *path );
LONG WINAPI GetPackagePathByFullName( const WCHAR *name, UINT32 *len, WCHAR *path );
LONG WINAPI PackageFamilyNameFromFullName( const WCHAR *full_name, UINT32 *length, WCHAR *family_name );
LONG WINAPI PackageIdFromFullName( const WCHAR *full_name, UINT32 flags, UINT32 *buffer_length, BYTE *buffer );

/*
 * Package identity, two layers.
 *
 *  1. Env vars (WINE_PACKAGE_FULL_NAME / WINE_PACKAGE_FAMILY_NAME / WINE_PACKAGE_PATH /
 *     WINE_PACKAGE_AUMID / WINE_FRAMEWORK_PACKAGE_*) pin an identity per run; a gate
 *     sets them to override everything.
 *  2. Below the env vars sits the package registration -- the facts the Windows
 *     apprepository service keeps in its StateRepository, in the registry, seeded
 *     per prefix (the layout Wine's apprepository consumers read):
 *
 *       <root>\Software\Microsoft\Windows NT\CurrentVersion\AppModel\
 *         Exes\<exe base name>   PackageFullName, PackagePath, ApplicationUserModelId
 *         Identity               same values; the fallback for unregistered images
 *         Packages\<FullName>    PackagePath, Framework (REG_DWORD 1 = framework pkg)
 *
 *     Lookups go HKCU first, then HKLM, mirroring the per-user package view over
 *     the machine registration. This is what makes GetCurrentPackageFullName,
 *     GetCurrentApplicationUserModelId, the path APIs and the family enumeration
 *     answer the package the app was deployed as (WhatsApp.Root.exe ->
 *     5319275A.WhatsAppDesktop_..., CalculatorApp.exe -> Microsoft.WindowsCalculator_...)
 *     with no per-launch environment.
 */
static const WCHAR appmodel_reg_path[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel";

/* Copy src into the caller's length-prefixed string buffer, Windows style. */
static LONG package_fill_str( const WCHAR *src, UINT32 *length, WCHAR *out )
{
    UINT32 len = wcslen( src ) + 1; /* NUL is counted in the contract */

    if (!length) return ERROR_INVALID_PARAMETER;
    if (!out || *length < len)
    {
        *length = len;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy( out, src, len * sizeof(WCHAR) );
    *length = len;
    return ERROR_SUCCESS;
}

static BOOL get_current_exe_name( WCHAR *buf, DWORD cch )
{
    WCHAR path[MAX_PATH];
    DWORD len = GetModuleFileNameW( 0, path, ARRAY_SIZE(path) );
    WCHAR *p;

    if (!len || len >= ARRAY_SIZE(path)) return FALSE;
    if ((p = wcsrchr( path, L'\\' ))) len -= (DWORD)(p + 1 - path), memmove( path, p + 1, (len + 1) * sizeof(WCHAR) );
    if (len >= cch) return FALSE;
    memcpy( buf, path, (len + 1) * sizeof(WCHAR) );
    return TRUE;
}

/* Read a REG_SZ from <root>\<appmodel path>\<subkey> (subkey may be NULL). */
static BOOL appmodel_read_str( HKEY root, const WCHAR *subkey, const WCHAR *value, WCHAR *buf, DWORD cch )
{
    WCHAR path[600];
    HKEY hkey;
    DWORD type = 0, size;
    LONG ret;

    lstrcpyW( path, appmodel_reg_path );
    if (subkey && subkey[0]) { lstrcatW( path, L"\\" ); lstrcatW( path, subkey ); }
    if (RegOpenKeyExW( root, path, 0, KEY_READ, &hkey )) return FALSE;
    buf[0] = 0;
    size = cch * sizeof(WCHAR);
    ret = RegQueryValueExW( hkey, value, NULL, &type, (BYTE *)buf, &size );
    RegCloseKey( hkey );
    return ret == ERROR_SUCCESS && type == REG_SZ;
}

/* Registration string for this process: Exes\<image> wins, Identity is the
 * fallback; HKCU wins over HKLM at each step. */
static BOOL appmodel_get_str( const WCHAR *value, WCHAR *buf, DWORD cch )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR subkey[576], exe[288];
    SIZE_T i;

    if (get_current_exe_name( exe, ARRAY_SIZE(exe) ))
    {
        lstrcpyW( subkey, L"Exes\\" );
        lstrcatW( subkey, exe );
        for (i = 0; i < ARRAY_SIZE(roots); i++)
            if (appmodel_read_str( roots[i], subkey, value, buf, cch )) return TRUE;
    }
    for (i = 0; i < ARRAY_SIZE(roots); i++)
        if (appmodel_read_str( roots[i], L"Identity", value, buf, cch )) return TRUE;
    return FALSE;
}

static LONG package_str_from_registry( const WCHAR *value, UINT32 *length, WCHAR *out )
{
    WCHAR buf[512];

    if (!appmodel_get_str( value, buf, ARRAY_SIZE(buf) )) return APPMODEL_ERROR_NO_PACKAGE;
    return package_fill_str( buf, length, out );
}

/* Env var first (per-run override), registration second. */
static LONG package_str( const WCHAR *env_var, const WCHAR *reg_value, UINT32 *length, WCHAR *out )
{
    LONG ret = package_str_from_env( env_var, length, out );

    if (ret != APPMODEL_ERROR_NO_PACKAGE) return ret;
    return package_str_from_registry( reg_value, length, out );
}

/* Path of a registered package: the env paths describe the running package and
 * its env framework; the registration covers every deployed package. */
static BOOL package_path_for( const WCHAR *full_name, WCHAR *buf, DWORD cch )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR cur[512] = L"", fw[512] = L"", subkey[576];
    SIZE_T i;

    get_current_package_full_name( cur, ARRAY_SIZE(cur) );
    if (cur[0] && !wcsicmp( cur, full_name ) &&
        GetEnvironmentVariableW( L"WINE_PACKAGE_PATH", buf, cch )) return TRUE;
    if (GetEnvironmentVariableW( L"WINE_FRAMEWORK_PACKAGE_FULL_NAME", fw, ARRAY_SIZE(fw) ) &&
        !wcsicmp( fw, full_name ) &&
        GetEnvironmentVariableW( L"WINE_FRAMEWORK_PACKAGE_PATH", buf, cch )) return TRUE;
    if (cur[0] && !wcsicmp( cur, full_name ) &&
        appmodel_get_str( L"PackagePath", buf, cch )) return TRUE;
    lstrcpyW( subkey, L"Packages\\" );
    lstrcatW( subkey, full_name );
    for (i = 0; i < ARRAY_SIZE(roots); i++)
    {
        /* the deployment records the install path; PackageRootPath is the
         * root the installer saw, an acceptable answer for a package that
         * only records that one */
        if (appmodel_read_str( roots[i], subkey, L"PackagePath", buf, cch )) return TRUE;
        if (appmodel_read_str( roots[i], subkey, L"PackageRootPath", buf, cch )) return TRUE;
    }
    return FALSE;
}

/* The registered package set: the running package, the env framework (when
 * pinned), and every entry of the Packages registration. A family filter
 * narrows the result; a NULL filter collects all. Entries fill full/path/fam. */
#define MAX_PKG_ENTRIES 16

struct pkg_entry
{
    WCHAR full[512];
    WCHAR path[512];
    WCHAR fam[256];
    BOOL framework;
};

static BOOL pkg_entry_add( struct pkg_entry *entries, UINT32 *n, const WCHAR *full, BOOL framework )
{
    struct pkg_entry *e;
    UINT32 i, len = ARRAY_SIZE(e->path);

    for (i = 0; i < *n; i++)
        if (!wcsicmp( entries[i].full, full )) return FALSE;
    if (*n == MAX_PKG_ENTRIES) return FALSE;

    e = &entries[*n];
    lstrcpyW( e->full, full );
    e->framework = framework;
    if (!package_path_for( full, e->path, ARRAY_SIZE(e->path) )) e->path[0] = 0;
    len = ARRAY_SIZE(e->fam);
    if (PackageFamilyNameFromFullName( full, &len, e->fam )) e->fam[0] = 0;
    (*n)++;
    return TRUE;
}

static void appmodel_enum_packages( struct pkg_entry *entries, UINT32 *n )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR path[600], name[512], subkey[576];
    SIZE_T r;
    DWORD i, framework;

    for (r = 0; r < ARRAY_SIZE(roots); r++)
    {
        HKEY hkey;

        lstrcpyW( path, appmodel_reg_path );
        lstrcatW( path, L"\\Packages" );
        if (RegOpenKeyExW( roots[r], path, 0, KEY_READ, &hkey )) continue;
        for (i = 0; *n < MAX_PKG_ENTRIES; i++)
        {
            DWORD len = ARRAY_SIZE(name);
            HKEY pkg;

            if (RegEnumKeyExW( hkey, i, name, &len, NULL, NULL, NULL, NULL )) break;
            lstrcpyW( subkey, path );
            lstrcatW( subkey, L"\\" );
            lstrcatW( subkey, name );
            framework = 0;
            if (RegOpenKeyExW( roots[r], subkey, 0, KEY_READ, &pkg ) == ERROR_SUCCESS)
            {
                DWORD size = sizeof(framework), type = 0;
                RegQueryValueExW( pkg, L"Framework", NULL, &type, (BYTE *)&framework, &size );
                RegCloseKey( pkg );
            }
            pkg_entry_add( entries, n, name, framework == 1 );
        }
        RegCloseKey( hkey );
    }
}

/* The registered package set, narrowed to one family when family is non-NULL:
 * the running package first, then the env framework, then the registration. */
static UINT32 collect_packages( struct pkg_entry *entries, const WCHAR *family )
{
    WCHAR full[512], fw[512];
    UINT32 n = 0, i, j;

    if (get_current_package_full_name( full, ARRAY_SIZE(full) )) pkg_entry_add( entries, &n, full, FALSE );
    if (GetEnvironmentVariableW( L"WINE_FRAMEWORK_PACKAGE_FULL_NAME", fw, ARRAY_SIZE(fw) ))
        pkg_entry_add( entries, &n, fw, TRUE );
    appmodel_enum_packages( entries, &n );

    if (!family) return n;
    for (i = 0, j = 0; i < n; i++)
    {
        if (entries[i].fam[0] && !wcsicmp( entries[i].fam, family ))
            entries[j++] = entries[i];
    }
    return j;
}

/***********************************************************************
 *         GetCurrentApplicationUserModelId   (kernelbase.@)
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetCurrentApplicationUserModelId( UINT32 *length, WCHAR *id )
{
    LONG ret = package_str( L"WINE_PACKAGE_AUMID", L"ApplicationUserModelId", length, id );
    if (ret == APPMODEL_ERROR_NO_PACKAGE)
    {
        FIXME( "(%p %p): stub\n", length, id );
        return APPMODEL_ERROR_NO_APPLICATION;
    }
    return ret;
}

/* EXPERIMENT: source package identity from env vars so packaged apps (WhatsApp
 * WinUI3) get a non-empty package context. Set WINE_PACKAGE_FULL_NAME /
 * WINE_PACKAGE_FAMILY_NAME when launching. */
static LONG package_str_from_env( const WCHAR *var, UINT32 *length, WCHAR *name )
{
    WCHAR buf[512];
    DWORD len = GetEnvironmentVariableW( var, buf, ARRAY_SIZE(buf) );
    if (!len || len >= ARRAY_SIZE(buf)) return APPMODEL_ERROR_NO_PACKAGE;
    if (!length) return ERROR_INVALID_PARAMETER;
    return package_fill_str( buf, length, name );
}

/***********************************************************************
 *         GetCurrentPackageFamilyName   (kernelbase.@)
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetCurrentPackageFamilyName( UINT32 *length, WCHAR *name )
{
    WCHAR full[512], fam[256];
    UINT32 len = ARRAY_SIZE(fam);
    LONG ret = package_str( L"WINE_PACKAGE_FAMILY_NAME", L"PackageFamilyName", length, name );

    if (ret != APPMODEL_ERROR_NO_PACKAGE) return ret;
    /* a package with an identity has a family: derive it from the full name */
    if (get_current_package_full_name( full, ARRAY_SIZE(full) ) &&
        !PackageFamilyNameFromFullName( full, &len, fam ))
        return package_fill_str( fam, length, name );
    FIXME( "(%p %p): no package\n", length, name );
    return ret;
}


/***********************************************************************
 *         GetCurrentPackageFullName   (kernelbase.@)
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetCurrentPackageFullName( UINT32 *length, WCHAR *name )
{
    LONG ret = package_str( L"WINE_PACKAGE_FULL_NAME", L"PackageFullName", length, name );
    if (ret == APPMODEL_ERROR_NO_PACKAGE) FIXME( "(%p %p): no package\n", length, name );
    return ret;
}


/* get the current package full name (env var, then registration) into a stack buffer */
static BOOL get_current_package_full_name( WCHAR *buf, UINT32 cch )
{
    UINT32 len = cch;
    return GetCurrentPackageFullName( &len, buf ) == ERROR_SUCCESS;
}

/***********************************************************************
 *         GetCurrentPackageId   (kernelbase.@)
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetCurrentPackageId( UINT32 *len, BYTE *buffer )
{
    WCHAR full_name[512];

    if (!get_current_package_full_name( full_name, ARRAY_SIZE(full_name) ))
    {
        FIXME( "(%p %p): no package\n", len, buffer );
        return APPMODEL_ERROR_NO_PACKAGE;
    }
    return PackageIdFromFullName( full_name, 0, len, buffer );
}

/***********************************************************************
 *         GetCurrentPackageInfo   (kernelbase.@)
 *
 * Build the PACKAGE_INFO array for the registered package set -- the running
 * package first, then its frameworks. The app uses this to discover its own
 * package layout; entries carry the identity fields, path, family, and the
 * framework flag.
 */
LONG WINAPI PackageFamilyNameFromFullName(const WCHAR *full_name, UINT32 *length, WCHAR *family_name);

LONG WINAPI GetCurrentPackageInfo( const UINT32 flags, UINT32 *buffer_size, BYTE *buffer, UINT32 *count )
{
    struct pkg_entry entries[MAX_PKG_ENTRIES];
    WCHAR full[512], fw[512];
    BYTE id_buf[512];
    PACKAGE_INFO *info;
    SIZE_T need = 0, str_off;
    UINT32 n, i, id_len;
    LONG ret;

    if (!buffer_size) return ERROR_INVALID_PARAMETER;

    n = 0;
    if (get_current_package_full_name( full, ARRAY_SIZE(full) )) pkg_entry_add( entries, &n, full, FALSE );
    if (GetEnvironmentVariableW( L"WINE_FRAMEWORK_PACKAGE_FULL_NAME", fw, ARRAY_SIZE(fw) ))
        pkg_entry_add( entries, &n, fw, TRUE );
    if (!n)
    {
        FIXME( "(%#x %p %p %p): no package\n", flags, buffer_size, buffer, count );
        return APPMODEL_ERROR_NO_PACKAGE;
    }

    str_off = n * sizeof(PACKAGE_INFO);
    need = str_off;
    for (i = 0; i < n; i++)
    {
        PACKAGE_ID *id = (PACKAGE_ID *)id_buf;
        id_len = sizeof(id_buf);
        if ((ret = PackageIdFromFullName( entries[i].full, 0, &id_len, id_buf ))) return ret;
        need += (wcslen( entries[i].full ) + 1) * sizeof(WCHAR);
        need += (wcslen( entries[i].path ) + 1) * sizeof(WCHAR);
        if (entries[i].fam[0]) need += (wcslen( entries[i].fam ) + 1) * sizeof(WCHAR);
        need += (id->name ? (wcslen(id->name)+1)*sizeof(WCHAR) : 0);
        need += (id->publisher ? (wcslen(id->publisher)+1)*sizeof(WCHAR) : 0);
        need += (id->resourceId ? (wcslen(id->resourceId)+1)*sizeof(WCHAR) : 0);
        need += (id->publisherId ? (wcslen(id->publisherId)+1)*sizeof(WCHAR) : 0);
    }

    if (count) *count = n;
    if (*buffer_size < need || !buffer)
    {
        *buffer_size = need;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    info = (PACKAGE_INFO *)buffer;
    memset( info, 0, n * sizeof(PACKAGE_INFO) );
    {
        BYTE *p = buffer + str_off;
        for (i = 0; i < n; i++)
        {
            PACKAGE_ID *id = (PACKAGE_ID *)id_buf;
            SIZE_T b;

            id_len = sizeof(id_buf);
            if (PackageIdFromFullName( entries[i].full, 0, &id_len, id_buf )) continue;

            info[i].flags = entries[i].framework ? 0x00000001 /* PACKAGE_INFORMATION_FRAMEWORK */ : 0;
            b = (wcslen( entries[i].path ) + 1) * sizeof(WCHAR);
            info[i].path = (WCHAR *)p; memcpy( p, entries[i].path, b ); p += b;
            b = (wcslen( entries[i].full ) + 1) * sizeof(WCHAR);
            info[i].packageFullName = (WCHAR *)p; memcpy( p, entries[i].full, b ); p += b;
            if (entries[i].fam[0])
            {
                b = (wcslen( entries[i].fam ) + 1) * sizeof(WCHAR);
                info[i].packageFamilyName = (WCHAR *)p; memcpy( p, entries[i].fam, b ); p += b;
            }
            info[i].packageId = *id;
#define COPYSTR(field) do { if (id->field) { SIZE_T bb = (wcslen(id->field)+1)*sizeof(WCHAR); \
            info[i].packageId.field = (WCHAR *)p; memcpy( p, id->field, bb ); p += bb; } } while(0)
            COPYSTR(name); COPYSTR(publisher); COPYSTR(resourceId); COPYSTR(publisherId);
#undef COPYSTR
        }
    }
    *buffer_size = need;
    return ERROR_SUCCESS;
}

/***********************************************************************
 *         GetCurrentPackagePath   (kernelbase.@)
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetCurrentPackagePath( UINT32 *length, WCHAR *path )
{
    WCHAR full_name[512];

    if (!get_current_package_full_name( full_name, ARRAY_SIZE(full_name) ))
    {
        FIXME( "(%p %p): no package\n", length, path );
        return APPMODEL_ERROR_NO_PACKAGE;
    }
    return GetPackagePathByFullName( full_name, length, path );
}


/***********************************************************************
 *         GetPackageFullName   (kernelbase.@)
 *
 * The current process answers from its own identity; any other process is
 * matched by its image name against the Exes registration.
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetPackageFullName( HANDLE process, UINT32 *length, WCHAR *name )
{
    static const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR exe[MAX_PATH], subkey[576], full[512];
    SIZE_T i;
    DWORD len = ARRAY_SIZE(exe);

    if (!process || process == GetCurrentProcess()) return GetCurrentPackageFullName( length, name );

    if (QueryFullProcessImageNameW( process, 0, exe, &len ))
    {
        WCHAR *p = wcsrchr( exe, L'\\' );
        lstrcpyW( subkey, L"Exes\\" );
        lstrcatW( subkey, p ? p + 1 : exe );
        for (i = 0; i < ARRAY_SIZE(roots); i++)
            if (appmodel_read_str( roots[i], subkey, L"PackageFullName", full, ARRAY_SIZE(full) ))
                return package_fill_str( full, length, name );
    }
    FIXME( "(%p %p %p): no package\n", process, length, name );
    return APPMODEL_ERROR_NO_PACKAGE;
}


/***********************************************************************
 *         GetPackageFamilyName   (kernelbase.@)
 */
LONG WINAPI /* DECLSPEC_HOTPATCH */ GetPackageFamilyName( HANDLE process, UINT32 *length, WCHAR *name )
{
    WCHAR full[512], fam[256];
    UINT32 len;
    LONG ret;

    if (!process || process == GetCurrentProcess()) return GetCurrentPackageFamilyName( length, name );

    len = ARRAY_SIZE(full);
    if ((ret = GetPackageFullName( process, &len, full ))) return ret;
    len = ARRAY_SIZE(fam);
    if (PackageFamilyNameFromFullName( full, &len, fam )) return APPMODEL_ERROR_NO_PACKAGE;
    return package_fill_str( fam, length, name );
}

/***********************************************************************
 *         GetPackagesByPackageFamily   (kernelbase.@)
 *
 * Enumerates the packages registered for the family: the deployment view is
 * the Packages registration plus the env-pinned running package and framework.
 * The names come back as consecutive NUL-terminated strings in full_names.
 */
LONG WINAPI DECLSPEC_HOTPATCH GetPackagesByPackageFamily(const WCHAR *family_name, UINT32 *count,
                                                         WCHAR *full_names, UINT32 *buffer_len, WCHAR *buffer)
{
    struct pkg_entry entries[MAX_PKG_ENTRIES];
    UINT32 n, i, total = 0;

    TRACE( "(%s %p %p %p %p)\n", debugstr_w(family_name), count, full_names, buffer_len, buffer );

    if (!count || !buffer_len) return ERROR_INVALID_PARAMETER;
    if (!family_name) return ERROR_INVALID_PARAMETER;

    n = collect_packages( entries, family_name );
    for (i = 0; i < n; i++) total += wcslen( entries[i].full ) + 1;

    if (full_names && *count >= n && *buffer_len >= total)
    {
        WCHAR *p = full_names;
        for (i = 0; i < n; i++)
        {
            SIZE_T chars = wcslen( entries[i].full ) + 1;
            memcpy( p, entries[i].full, chars * sizeof(WCHAR) );
            p += chars;
        }
    }
    else if (n)
    {
        *count = n;
        *buffer_len = total;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    *count = n;
    *buffer_len = total;
    return ERROR_SUCCESS;
}

/* The per-user deployment view shared by the GetPackagesForUser family. The
 * prefix has one user, so `user` selects nothing; `types` is the PACKAGE_TYPE
 * bitmask (0 = both) and `path_type` picks which path the registration knows
 * (it records one PackagePath, so all three answer it). The names come back as
 * one LocalAlloc'd block of consecutive NUL-terminated strings, which the
 * caller releases with LocalFree, as on Windows; buffer_len (when given) is the
 * block length in characters. */
static LONG packages_for_user( const WCHAR *user, const WCHAR *family_name, UINT32 path_type,
                               UINT32 types, UINT32 *count, WCHAR **buffer, UINT32 *buffer_len )
{
    static int once_user, once_path;
    struct pkg_entry entries[MAX_PKG_ENTRIES];
    UINT32 n, i, j, total = 0;
    WCHAR *block, *p;

    if (user && !once_user++) FIXME( "%s: the prefix has one user, the whole registration is theirs.\n", debugstr_w( user ) );
    if (path_type && !once_path++) FIXME( "path type %u answers the registered package path.\n", path_type );
    if (!count) return ERROR_INVALID_PARAMETER;
    if (buffer) *buffer = NULL;
    if (buffer_len) *buffer_len = 0;

    n = collect_packages( entries, family_name );
    for (i = 0, j = 0; i < n; i++)
    {
        UINT32 type = entries[i].framework ? FRAMEWORK : PACKAGE;
        if (!types || (types & type)) entries[j++] = entries[i];
    }
    n = j;

    for (i = 0; i < n; i++) total += wcslen( entries[i].full ) + 1;
    *count = n;
    if (buffer_len) *buffer_len = total;
    if (!buffer || !n) return ERROR_SUCCESS;

    if (!(block = LocalAlloc( 0, total * sizeof(WCHAR) ))) return ERROR_NOT_ENOUGH_MEMORY;
    p = block;
    for (i = 0; i < n; i++)
    {
        SIZE_T chars = wcslen( entries[i].full ) + 1;
        memcpy( p, entries[i].full, chars * sizeof(WCHAR) );
        p += chars;
    }
    *buffer = block;
    return ERROR_SUCCESS;
}

/***********************************************************************
 *         GetPackagesForUser   (kernelbase.@)
 *
 * The packages registered for a user (see GetPackagesByPackageFamily for the
 * registration): a family narrows the set, NULL family name collects all of
 * it. Both package types answer; the caller frees the block with LocalFree.
 */
LONG WINAPI GetPackagesForUser( const WCHAR *user, const WCHAR *family_name,
                                UINT32 *count, WCHAR **buffer )
{
    TRACE( "(%s %s %p %p)\n", debugstr_w( user ), debugstr_w( family_name ), count, buffer );

    return packages_for_user( user, family_name, APPLICATION_PACKAGE_PATH, 0, count, buffer, NULL );
}

/***********************************************************************
 *         GetPackagesForUserWithPackageTypes   (kernelbase.@)
 *
 * GetPackagesForUser with the PACKAGE_TYPE bitmask filter and the path type;
 * the registration records one path per package, so every path type answers
 * that PackagePath, and the block carries the full names.
 */
LONG WINAPI GetPackagesForUserWithPackageTypes( const WCHAR *user, const WCHAR *family_name,
                                                UINT32 path_type, UINT32 package_types,
                                                UINT32 *count, WCHAR **buffer, UINT32 *buffer_len )
{
    TRACE( "(%s %s %u %#x %p %p %p)\n", debugstr_w( user ), debugstr_w( family_name ),
           path_type, package_types, count, buffer, buffer_len );

    return packages_for_user( user, family_name, path_type, package_types, count, buffer, buffer_len );
}

/***********************************************************************
 *         GetPackageFamilyNameForUser   (kernelbase.@)
 *
 * The family of a process's package, for a user the prefix only has one of:
 * the same answer GetPackageFamilyName gives.
 */
LONG WINAPI GetPackageFamilyNameForUser( const WCHAR *user, HANDLE process,
                                         UINT32 *length, WCHAR *family_name )
{
    static int once;

    TRACE( "(%s %p %p %p)\n", debugstr_w( user ), process, length, family_name );

    if (!once++ && user) FIXME( "%s: the prefix has one user.\n", debugstr_w( user ) );
    return GetPackageFamilyName( process, length, family_name );
}

/***********************************************************************
 *         GetPackagePathByFullName   (kernelbase.@)
 */
LONG WINAPI GetPackagePathByFullName(const WCHAR *name, UINT32 *len, WCHAR *path)
{
    WCHAR buf[MAX_PATH];

    if (!len || !name)
        return ERROR_INVALID_PARAMETER;

    /* env layer (WINE_PACKAGE_PATH, WINE_FRAMEWORK_PACKAGE_PATH) first, then
     * the PackagePath of the registered deployment */
    if (package_path_for( name, buf, ARRAY_SIZE(buf) )) return package_fill_str( buf, len, path );

    FIXME( "(%s %p %p): no package path\n", debugstr_w(name), len, path );
    return APPMODEL_ERROR_NO_PACKAGE;
}

/***********************************************************************
 *         GetPackagePathByFullName2   (kernelbase.@)
 */
LONG WINAPI GetPackagePathByFullName2(const WCHAR *name, UINT32 type, UINT32 *len, WCHAR *path)
{
    TRACE( "(%s %u %p %p)\n", debugstr_w(name), type, len, path );
    return GetPackagePathByFullName( name, len, path );
}


/*
 * Package identity v2/v3 layer.
 *
 * Extends the env-var driven v1 layer (WINE_PACKAGE_FULL_NAME / WINE_PACKAGE_PATH /
 * WINE_FRAMEWORK_PACKAGE_*) to the widened APIs packaged UWP apps ask for on
 * Win10+: 64-bit buffer sizes, the extended PACKAGE_INFO2/PACKAGE_INFO3 layouts,
 * package context strings, FindPackagesByPackageFamily, and CloseState.
 * The context strings are opaque on Windows; the only identity this layer has
 * is the full name, so the context APIs hand that back. Family queries answer
 * against the running package and its framework, the only packages there are.
 */

#define PACKAGE_INSTALL_STATE_AVAILABLE_ 0
#define PACKAGE_TARGET_DEVICE_FAMILY_    L"Windows.Desktop"
#define PACKAGE_INSTALLATION_LCID_       0x0409
#define PACKAGE_INFORMATION_FRAMEWORK_   0x00000001

static LONG current_package_info_ext( const UINT32 flags, SIZE_T buffer_size, BYTE *buffer,
                                      UINT32 *count, UINT32 version, SIZE_T entry_size )
{
    BYTE scratch[8192], *heap_scratch = NULL;
    UINT32 cnt = 0, i, j, sz;
    PACKAGE_INFO *src, *base;
    SIZE_T str_total = 0, out_need, off;
    LONG ret;

    sz = sizeof(scratch);
    ret = GetCurrentPackageInfo( flags, &sz, scratch, &cnt );
    if (ret == ERROR_INSUFFICIENT_BUFFER)
    {
        if (!(heap_scratch = HeapAlloc( GetProcessHeap(), 0, sz ))) return ERROR_OUTOFMEMORY;
        sz = heap_scratch ? sz : 0;
        ret = GetCurrentPackageInfo( flags, &sz, heap_scratch, &cnt );
        src = (PACKAGE_INFO *)heap_scratch;
    }
    else src = (PACKAGE_INFO *)scratch;
    if (ret)
    {
        HeapFree( GetProcessHeap(), 0, heap_scratch );
        return ret;
    }

    for (i = 0; i < cnt; i++)
    {
        const WCHAR *strs[7] = { src[i].path, src[i].packageFullName, src[i].packageFamilyName,
                                 src[i].packageId.name, src[i].packageId.publisher,
                                 src[i].packageId.resourceId, src[i].packageId.publisherId };
        for (j = 0; j < ARRAY_SIZE( strs ); j++)
            if (strs[j]) str_total += (wcslen( strs[j] ) + 1) * sizeof( WCHAR );
        if (version == 3) str_total += (wcslen( PACKAGE_TARGET_DEVICE_FAMILY_ ) + 1) * sizeof( WCHAR );
    }

    out_need = (SIZE_T)cnt * entry_size + str_total;
    if (count) *count = cnt;
    if (!buffer || buffer_size < out_need)
    {
        HeapFree( GetProcessHeap(), 0, heap_scratch );
        return ERROR_INSUFFICIENT_BUFFER;
    }

    off = (SIZE_T)cnt * entry_size;
    for (i = 0; i < cnt; i++)
    {
        BYTE *dst = buffer + (SIZE_T)i * entry_size;
        memcpy( dst, &src[i], sizeof(PACKAGE_INFO) );
        base = (PACKAGE_INFO *)dst;
        *(UINT32 *)(dst + sizeof(PACKAGE_INFO)) = PACKAGE_INSTALL_STATE_AVAILABLE_;
        if (version == 3)
        {
            *(UINT32 *)(dst + sizeof(PACKAGE_INFO) + 4) = PACKAGE_INSTALLATION_LCID_;
            *(const WCHAR **)(dst + sizeof(PACKAGE_INFO) + 8) = NULL;
        }
#define COPY_FIELD(f) do { if (base->f) { SIZE_T b = (wcslen( base->f ) + 1) * sizeof(WCHAR); \
            WCHAR *np = (WCHAR *)(buffer + off); memcpy( np, base->f, b ); off += b; base->f = np; } } while (0)
        COPY_FIELD( path );
        COPY_FIELD( packageFullName );
        COPY_FIELD( packageFamilyName );
#define COPY_IDFIELD(f) do { if (base->packageId.f) { SIZE_T b = (wcslen( base->packageId.f ) + 1) * sizeof(WCHAR); \
            WCHAR *np = (WCHAR *)(buffer + off); memcpy( np, base->packageId.f, b ); off += b; base->packageId.f = np; } } while (0)
        COPY_IDFIELD( name );
        COPY_IDFIELD( publisher );
        COPY_IDFIELD( resourceId );
        COPY_IDFIELD( publisherId );
#undef COPY_FIELD
#undef COPY_IDFIELD
        if (version == 3)
        {
            static const WCHAR tdf[] = PACKAGE_TARGET_DEVICE_FAMILY_;
            SIZE_T b = (wcslen( tdf ) + 1) * sizeof( WCHAR );
            WCHAR *np = (WCHAR *)(buffer + off);
            memcpy( np, tdf, b );
            off += b;
            *(const WCHAR **)(dst + sizeof(PACKAGE_INFO) + 8) = np;
        }
    }

    HeapFree( GetProcessHeap(), 0, heap_scratch );
    return ERROR_SUCCESS;
}

/***********************************************************************
 *         GetCurrentPackageInfo2   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackageInfo2( const UINT32 flags, UINT64 buffer_size, BYTE *buffer, UINT32 *count )
{
    return current_package_info_ext( flags, buffer_size, buffer, count, 2, sizeof(PACKAGE_INFO2) );
}

/***********************************************************************
 *         GetCurrentPackageInfo3   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackageInfo3( const UINT32 flags, UINT64 buffer_size, BYTE *buffer, UINT32 *count )
{
    return current_package_info_ext( flags, buffer_size, buffer, count, 3, sizeof(PACKAGE_INFO3) );
}

/***********************************************************************
 *         GetCurrentPackagePath2   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackagePath2( const UINT32 flags, UINT32 *length, WCHAR *path )
{
    TRACE( "(%#x %p %p)\n", flags, length, path );
    return GetCurrentPackagePath( length, path );
}

static LONG package_context_from_env( const UINT32 flags, UINT32 *length, WCHAR *buffer )
{
    static int once;
    if (!once++) FIXME( "package contexts are opaque on Windows; returning the package full name.\n" );
    return package_str( L"WINE_PACKAGE_FULL_NAME", L"PackageFullName", length, buffer );
}

/***********************************************************************
 *         GetCurrentPackageContext   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackageContext( const UINT32 flags, UINT32 *length, WCHAR *buffer )
{
    TRACE( "(%#x %p %p)\n", flags, length, buffer );
    return package_context_from_env( flags, length, buffer );
}

/***********************************************************************
 *         GetCurrentPackageApplicationContext   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackageApplicationContext( const UINT32 flags, UINT32 *length, WCHAR *buffer )
{
    TRACE( "(%#x %p %p)\n", flags, length, buffer );
    return package_context_from_env( flags, length, buffer );
}

/***********************************************************************
 *         GetCurrentPackageApplicationResourcesContext   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackageApplicationResourcesContext( const UINT32 flags, UINT32 *length, WCHAR *buffer )
{
    TRACE( "(%#x %p %p)\n", flags, length, buffer );
    return package_context_from_env( flags, length, buffer );
}

/***********************************************************************
 *         GetCurrentPackageGlobalizationContext   (kernelbase.@)
 */
LONG WINAPI GetCurrentPackageGlobalizationContext( const UINT32 flags, UINT32 *length, WCHAR *buffer )
{
    TRACE( "(%#x %p %p)\n", flags, length, buffer );
    return package_context_from_env( flags, length, buffer );
}

/***********************************************************************
 *         CloseState   (kernelbase.@)
 *
 * State handles belong to the StateRepository service, which is not here; the
 * SRPC/SRCache clients (see dlls/windows.staterepositorycore) treat a closed
 * handle as nothing, so closing is a no-op that reports success.
 */
LONG WINAPI CloseState( void *state )
{
    TRACE( "%p\n", state );
    return ERROR_SUCCESS;
}

/* The PACKAGE_INFO strings handed to FindPackagesByPackageFamily callers stay
 * live: ClosePackageInfo is a stub and the SRPC consumers keep reading the
 * fields they were given, so the strings are process-static copies. */
static WCHAR *dupw_static( const WCHAR *str )
{
    SIZE_T bytes;

    if (!str) return NULL;
    bytes = (wcslen( str ) + 1) * sizeof( WCHAR );
    WCHAR *p = HeapAlloc( GetProcessHeap(), 0, bytes );
    if (p) memcpy( p, str, bytes );
    return p;
}

static void fill_package_info_entry( PACKAGE_INFO *info, const WCHAR *full_name, const WCHAR *path, BOOL framework )
{
    BYTE id_buf[512];
    WCHAR fam[256];
    UINT32 id_len = sizeof(id_buf), fam_len = ARRAY_SIZE(fam);
    PACKAGE_ID *id = (PACKAGE_ID *)id_buf;

    memset( info, 0, sizeof(*info) );
    info->flags = framework ? PACKAGE_INFORMATION_FRAMEWORK_ : 0;
    info->packageFullName = dupw_static( full_name );
    info->path = dupw_static( path );
    if (!PackageFamilyNameFromFullName( full_name, &fam_len, fam )) info->packageFamilyName = dupw_static( fam );
    if (!PackageIdFromFullName( full_name, 0, &id_len, id_buf ))
    {
        info->packageId = *id;
        info->packageId.name = dupw_static( id->name );
        info->packageId.publisher = dupw_static( id->publisher );
        info->packageId.resourceId = dupw_static( id->resourceId );
        info->packageId.publisherId = dupw_static( id->publisherId );
    }
}

/***********************************************************************
 *         FindPackagesByPackageFamily   (kernelbase.@)
 *
 * Answers for the packages registered under the queried family: the running
 * package, the env framework, and the Packages registration. A family that
 * matches nothing resolves to nothing, which is what an uninstalled family
 * does on Windows.
 */
LONG WINAPI FindPackagesByPackageFamily( const WCHAR *family, UINT32 count, WCHAR *full_names,
                                         UINT32 *full_names_len, PACKAGE_INFO *package_info,
                                         UINT32 *package_info_count, void *filter )
{
    static int once;
    struct pkg_entry entries[MAX_PKG_ENTRIES];
    const WCHAR **names = NULL, **paths = NULL;
    UINT32 n = 0, total = 0, i;

    if (!family || !full_names_len) return ERROR_INVALID_PARAMETER;
    if (!once++) FIXME( "(%s %u %p %p %p %p %p): answering from the registered package set\n",
                        debugstr_w( family ), count, full_names, full_names_len, package_info,
                        package_info_count, filter );

    n = collect_packages( entries, family );
    if (!(names = HeapAlloc( GetProcessHeap(), 0, n * sizeof(*names) )) ||
        !(paths = HeapAlloc( GetProcessHeap(), 0, n * sizeof(*paths) )))
    {
        HeapFree( GetProcessHeap(), 0, names );
        HeapFree( GetProcessHeap(), 0, paths );
        return ERROR_OUTOFMEMORY;
    }
    for (i = 0; i < n; i++)
    {
        names[i] = entries[i].full;
        paths[i] = entries[i].path[0] ? entries[i].path : NULL;
        total += lstrlenW( names[i] ) + 1;
    }
    if (n > 1) total += n - 1;

    if (!count)  /* query: report the sizes, write nothing */
    {
        *full_names_len = total;
        if (package_info_count) *package_info_count = n;
        HeapFree( GetProcessHeap(), 0, names );
        HeapFree( GetProcessHeap(), 0, paths );
        return ERROR_SUCCESS;
    }

    if (full_names && *full_names_len >= total)
    {
        WCHAR *p = full_names;
        for (i = 0; i < n; i++)
        {
            if (i) *p++ = L' ';
            memcpy( p, names[i], (lstrlenW( names[i] ) + 1) * sizeof( WCHAR ) );
            p += lstrlenW( names[i] ) + 1;
        }
        *full_names_len = lstrlenW( full_names ) + 1;
    }
    else
    {
        if (full_names && total) full_names[0] = 0;
        *full_names_len = total;
        HeapFree( GetProcessHeap(), 0, names );
        HeapFree( GetProcessHeap(), 0, paths );
        return ERROR_INSUFFICIENT_BUFFER;
    }

    if (package_info_count)
    {
        UINT32 requested = *package_info_count;
        if (package_info)
        {
            if (requested < n)
            {
                *package_info_count = n;
                HeapFree( GetProcessHeap(), 0, names );
                HeapFree( GetProcessHeap(), 0, paths );
                return ERROR_INSUFFICIENT_BUFFER;
            }
            for (i = 0; i < n; i++)
                fill_package_info_entry( &package_info[i], names[i], paths[i], entries[i].framework );
        }
        *package_info_count = n;
    }
    HeapFree( GetProcessHeap(), 0, names );
    HeapFree( GetProcessHeap(), 0, paths );
    return ERROR_SUCCESS;
}
/*
 * Package-info blocks.
 *
 * OpenPackageInfoByFullName hands the caller a single heap block: a tagged
 * header, the PACKAGE_INFO struct, then every string the struct points at. That
 * layout lets ClosePackageInfo release what Open allocated in one HeapFree, and
 * the tag is what tells our blocks apart from the array entries a
 * FindPackagesByPackageFamily caller owns -- for those Windows releases the
 * strings, not the entry, so tagging keeps us from freeing caller memory.
 */
#define PACKAGE_INFO_BLOCK_TAG 0x57696e5061636b31ULL /* "WinPack1" */

struct package_info_block
{
    UINT64 tag;
    UINT32 reserved[2];
    PACKAGE_INFO info;
    WCHAR strings[];
};

static void copy_str_to_block( struct package_info_block *block, SIZE_T *cch, WCHAR **field, const WCHAR *str )
{
    SIZE_T len;

    if (!str) { *field = NULL; return; }
    len = wcslen( str ) + 1;
    *field = block->strings + *cch;
    memcpy( *field, str, len * sizeof( WCHAR ) );
    *cch += len;
}

static void free_package_info_strings( PACKAGE_INFO *info )
{
    WCHAR **fields[] = { &info->path, &info->packageFullName, &info->packageFamilyName,
                         &info->packageId.name, &info->packageId.publisher,
                         &info->packageId.resourceId, &info->packageId.publisherId };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE( fields ); i++)
    {
        HeapFree( GetProcessHeap(), 0, fields[i][0] );
        fields[i][0] = NULL;
    }
}

/* One package-info block for full_name + path, carrying the same identity fields
 * GetCurrentPackageInfo and FindPackagesByPackageFamily produce. */
static HRESULT package_info_block_from_name( const WCHAR *full_name, const WCHAR *path,
                                             struct package_info_block **ret )
{
    WCHAR family[256] = L"";
    UINT32 family_len = ARRAY_SIZE( family ), id_len = 0;
    struct package_info_block *block;
    PACKAGE_ID *id;
    BYTE *id_buffer;
    SIZE_T strings = 0, cch = 0;

    if (PackageIdFromFullName( full_name, 0, &id_len, NULL ) != ERROR_INSUFFICIENT_BUFFER)
        return HRESULT_FROM_WIN32( APPMODEL_ERROR_PACKAGE_IDENTITY_CORRUPT );
    if (!(id_buffer = HeapAlloc( GetProcessHeap(), 0, id_len ))) return E_OUTOFMEMORY;
    if (PackageIdFromFullName( full_name, 0, &id_len, id_buffer ))
    {
        HeapFree( GetProcessHeap(), 0, id_buffer );
        return HRESULT_FROM_WIN32( APPMODEL_ERROR_PACKAGE_IDENTITY_CORRUPT );
    }
    id = (PACKAGE_ID *)id_buffer;

    if (PackageFamilyNameFromFullName( full_name, &family_len, family )) family[0] = 0;

    strings = wcslen( full_name ) + 1;
    if (path) strings += wcslen( path ) + 1;
    if (family[0]) strings += wcslen( family ) + 1;
    if (id->name) strings += wcslen( id->name ) + 1;
    if (id->publisher) strings += wcslen( id->publisher ) + 1;
    if (id->resourceId) strings += wcslen( id->resourceId ) + 1;
    if (id->publisherId) strings += wcslen( id->publisherId ) + 1;

    if (!(block = HeapAlloc( GetProcessHeap(), 0, sizeof(*block) + strings * sizeof( WCHAR ) )))
    {
        HeapFree( GetProcessHeap(), 0, id_buffer );
        return E_OUTOFMEMORY;
    }
    memset( block, 0, sizeof(*block) );
    block->tag = PACKAGE_INFO_BLOCK_TAG;
    block->info.packageId = *id;
    copy_str_to_block( block, &cch, &block->info.path, path );
    copy_str_to_block( block, &cch, &block->info.packageFullName, full_name );
    copy_str_to_block( block, &cch, &block->info.packageFamilyName, family[0] ? family : NULL );
    copy_str_to_block( block, &cch, &block->info.packageId.name, id->name );
    copy_str_to_block( block, &cch, &block->info.packageId.publisher, id->publisher );
    copy_str_to_block( block, &cch, &block->info.packageId.resourceId, id->resourceId );
    copy_str_to_block( block, &cch, &block->info.packageId.publisherId, id->publisherId );

    HeapFree( GetProcessHeap(), 0, id_buffer );
    *ret = block;
    return S_OK;
}

/***********************************************************************
 *         OpenPackageInfoByFullName   (kernelbase.@)
 *
 * Opens a package of the registered set (see GetCurrentPackageFullName): the
 * running package, the env framework, and every entry of the Packages
 * registration. The PACKAGE_INFO handed back carries the fields
 * GetCurrentPackageInfo builds, path included, and ClosePackageInfo releases it.
 *
 * A name that does not parse as a package full name reports the appmodel
 * identity-corrupt code, a well-formed name of any other package the appmodel
 * no-package code (include/winerror.h: APPMODEL_ERROR_PACKAGE_IDENTITY_CORRUPT,
 * APPMODEL_ERROR_NO_PACKAGE); the documented return of this API is an HRESULT.
 */
LONG WINAPI OpenPackageInfoByFullName( const WCHAR *full_name, UINT32 reserved, PACKAGE_INFO **info )
{
    WCHAR current[512], path[512];
    struct package_info_block *block;
    UINT32 id_len = 0;
    HRESULT hr;

    TRACE( "(%s %#x %p)\n", debugstr_w( full_name ), reserved, info );

    if (!full_name || !info) return E_INVALIDARG;
    *info = NULL;
    if (reserved) FIXME( "(%s %#x %p): reserved argument is not supported.\n", debugstr_w( full_name ), reserved, info );

    /* shape first, so a malformed name is told apart from an unknown package */
    if (PackageIdFromFullName( full_name, 0, &id_len, NULL ) != ERROR_INSUFFICIENT_BUFFER)
        return HRESULT_FROM_WIN32( APPMODEL_ERROR_PACKAGE_IDENTITY_CORRUPT );

    if (!package_path_for( full_name, path, ARRAY_SIZE( path ) ))
    {
        /* the running package opens even with no install path recorded */
        if (!get_current_package_full_name( current, ARRAY_SIZE( current ) ) || wcsicmp( current, full_name ))
            return HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE );
        path[0] = 0;
    }
    if ((hr = package_info_block_from_name( full_name, path[0] ? path : NULL, &block )) != S_OK) return hr;
    *info = &block->info;
    return S_OK;
}


/***********************************************************************
 *         OpenPackageInfoByFullNameForUser   (kernelbase.@)
 *
 * Opens a package of a user's registration; the prefix has one user, so the
 * whole registration is theirs and the name-only entry answers: the same
 * package-info block, the same shape, released by ClosePackageInfo.
 */
LONG WINAPI OpenPackageInfoByFullNameForUser( const WCHAR *full_name, const WCHAR *user,
                                              UINT32 reserved, PACKAGE_INFO **info )
{
    static int once;

    TRACE( "(%s %s %#x %p)\n", debugstr_w( full_name ), debugstr_w( user ), reserved, info );

    if (!once++ && user) FIXME( "%s: the prefix has one user.\n", debugstr_w( user ) );
    return OpenPackageInfoByFullName( full_name, reserved, info );
}


/***********************************************************************
 *         ClosePackageInfo   (kernelbase.@)
 *
 * Releases a block OpenPackageInfoByFullName handed back. An entry the caller
 * owns (from FindPackagesByPackageFamily) keeps its struct -- only the strings
 * we allocated for it are released, which is what Windows does for those.
 */
LONG WINAPI ClosePackageInfo( PACKAGE_INFO *info )
{
    struct package_info_block *block = CONTAINING_RECORD( info, struct package_info_block, info );

    TRACE( "%p\n", info );

    if (!info) return E_INVALIDARG;
    if (block->tag == PACKAGE_INFO_BLOCK_TAG)
    {
        HeapFree( GetProcessHeap(), 0, block );
        return S_OK;
    }
    free_package_info_strings( info );
    return S_OK;
}


/***********************************************************************
 *         GetPackageApplicationIds   (kernelbase.@)
 *
 * Answers with the packages registered under the queried family -- the running
 * package, the env framework, and the Packages registration; the same set
 * FindPackagesByPackageFamily reports. The enumeration type GUID is not
 * discriminated. Passing no array is the count query. Each HPKG is a
 * package-info block ClosePackageInfo releases; the array itself Windows frees
 * with FreePackageEnum, which is not implemented here.
 */
LONG WINAPI GetPackageApplicationIds( const WCHAR *family_name, GUID package_enumeration_type,
                                      UINT32 *count, HPKG *packages )
{
    static int once;
    struct pkg_entry entries[MAX_PKG_ENTRIES];
    UINT32 n, i, j;
    HPKG *handles = NULL;

    TRACE( "(%s %s %p %p)\n", debugstr_w( family_name ), debugstr_guid( &package_enumeration_type ), count, packages );

    if (!family_name || !count) return E_INVALIDARG;
    if (packages) *packages = NULL;
    if (!once++) FIXME( "(%s %s %p %p): the enumeration type is not discriminated.\n",
                        debugstr_w( family_name ), debugstr_guid( &package_enumeration_type ), count, packages );

    n = collect_packages( entries, family_name );
    *count = n;
    if (!packages || !n) return ERROR_SUCCESS;

    if (!(handles = HeapAlloc( GetProcessHeap(), 0, n * sizeof(*handles) ))) return E_OUTOFMEMORY;
    for (i = 0; i < n; i++)
    {
        struct package_info_block *block;
        HRESULT hr = package_info_block_from_name( entries[i].full,
                                                   entries[i].path[0] ? entries[i].path : NULL, &block );

        if (hr != S_OK)
        {
            for (j = 0; j < i; j++) ClosePackageInfo( (PACKAGE_INFO *)handles[j] );
            HeapFree( GetProcessHeap(), 0, handles );
            return hr;
        }
        handles[i] = (HPKG)&block->info;
    }
    *packages = (HPKG)handles;
    return ERROR_SUCCESS;
}


/***********************************************************************
 *         IsMrtResourceRedirectionEnabled   (kernelbase.@)
 */
BOOL WINAPI IsMrtResourceRedirectionEnabled(void)
{
    return FALSE;
}

/***********************************************************************
 *         IsDeveloperModeEnabled   (kernelbase.@)
 */
HRESULT WINAPI IsDeveloperModeEnabled( BOOL *enabled )
{
    TRACE( "enabled %p\n", enabled );
    if (!enabled) return E_INVALIDARG;
    *enabled = FALSE;
    return S_OK;
}

static const struct
{
    UINT32 code;
    const WCHAR *name;
}
arch_names[] =
{
    {PROCESSOR_ARCHITECTURE_INTEL,         L"x86"},
    {PROCESSOR_ARCHITECTURE_ARM,           L"arm"},
    {PROCESSOR_ARCHITECTURE_AMD64,         L"x64"},
    {PROCESSOR_ARCHITECTURE_NEUTRAL,       L"neutral"},
    {PROCESSOR_ARCHITECTURE_ARM64,         L"arm64"},
    {PROCESSOR_ARCHITECTURE_UNKNOWN,       L"unknown"},
};

static UINT32 processor_arch_from_string(const WCHAR *str, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(arch_names); ++i)
        if (lstrlenW(arch_names[i].name) == len && !wcsnicmp(str, arch_names[i].name, len))
            return arch_names[i].code;
    return ~0u;
}

/***********************************************************************
 *         PackageIdFromFullName   (kernelbase.@)
 */
LONG WINAPI PackageIdFromFullName(const WCHAR *full_name, UINT32 flags, UINT32 *buffer_length, BYTE *buffer)
{
    const WCHAR *name, *version_str, *arch_str, *resource_id, *publisher_id, *s;
    PACKAGE_ID *id = (PACKAGE_ID *)buffer;
    UINT32 size, buffer_size, len;

    TRACE("full_name %s, flags %#x, buffer_length %p, buffer %p.\n",
            debugstr_w(full_name), flags, buffer_length, buffer);

    if (flags)
        FIXME("Flags %#x are not supported.\n", flags);

    if (!full_name || !buffer_length)
        return ERROR_INVALID_PARAMETER;

    if (!buffer && *buffer_length)
        return ERROR_INVALID_PARAMETER;

    name = full_name;
    if (!(version_str = wcschr(name, L'_')))
        return ERROR_INVALID_PARAMETER;
    ++version_str;

    if (!(arch_str = wcschr(version_str, L'_')))
        return ERROR_INVALID_PARAMETER;
    ++arch_str;

    if (!(resource_id = wcschr(arch_str, L'_')))
        return ERROR_INVALID_PARAMETER;
    ++resource_id;

    if (!(publisher_id = wcschr(resource_id, L'_')))
        return ERROR_INVALID_PARAMETER;
    ++publisher_id;

    /* Publisher id length should be 13. */
    size = sizeof(*id) + sizeof(WCHAR) * ((version_str - name) + (publisher_id - resource_id) + 13 + 1);
    buffer_size = *buffer_length;
    *buffer_length = size;
    if (buffer_size < size)
        return ERROR_INSUFFICIENT_BUFFER;

    memset(id, 0, sizeof(*id));
    if ((id->processorArchitecture = processor_arch_from_string(arch_str, resource_id - arch_str - 1)) == ~0u)
    {
        FIXME("Unrecognized arch %s.\n", debugstr_w(arch_str));
        return ERROR_INVALID_PARAMETER;
    }
    buffer += sizeof(*id);

    id->version.Major = wcstol(version_str, NULL, 10);
    if (!(s = wcschr(version_str, L'.')))
        return ERROR_INVALID_PARAMETER;
    ++s;
    id->version.Minor = wcstol(s, NULL, 10);
    if (!(s = wcschr(s, L'.')))
        return ERROR_INVALID_PARAMETER;
    ++s;
    id->version.Build = wcstol(s, NULL, 10);
    if (!(s = wcschr(s, L'.')))
        return ERROR_INVALID_PARAMETER;
    ++s;
    id->version.Revision = wcstol(s, NULL, 10);

    id->name = (WCHAR *)buffer;
    len = version_str - name - 1;
    memcpy(id->name, name, sizeof(*id->name) * len);
    id->name[len] = 0;
    buffer += sizeof(*id->name) * (len + 1);

    id->resourceId = (WCHAR *)buffer;
    len = publisher_id - resource_id - 1;
    memcpy(id->resourceId, resource_id, sizeof(*id->resourceId) * len);
    id->resourceId[len] = 0;
    buffer += sizeof(*id->resourceId) * (len + 1);

    id->publisherId = (WCHAR *)buffer;
    len = lstrlenW(publisher_id);
    if (len != 13)
        return ERROR_INVALID_PARAMETER;
    memcpy(id->publisherId, publisher_id, sizeof(*id->publisherId) * len);
    id->publisherId[len] = 0;

    return ERROR_SUCCESS;
}

/***********************************************************************
 *         PackageFamilyNameFromFullName   (kernelbase.@)
 */
LONG WINAPI PackageFamilyNameFromFullName(const WCHAR *full_name, UINT32 *length, WCHAR *family_name)
{
    const WCHAR *name, *version_str, *arch_str, *resource_id, *publisher_id;
    UINT32 name_len, pub_len, needed;

    TRACE("full_name %s, length %p, family_name %p.\n", debugstr_w(full_name), length, family_name);

    if (!full_name || !length)
        return ERROR_INVALID_PARAMETER;

    name = full_name;
    if (!(version_str = wcschr(name, L'_')))
        return ERROR_INVALID_PARAMETER;

    if (!(arch_str = wcschr(version_str + 1, L'_')))
        return ERROR_INVALID_PARAMETER;

    if (!(resource_id = wcschr(arch_str + 1, L'_')))
        return ERROR_INVALID_PARAMETER;

    if (!(publisher_id = wcschr(resource_id + 1, L'_')))
        return ERROR_INVALID_PARAMETER;
    ++publisher_id;

    name_len = version_str - name;
    pub_len = wcslen(publisher_id);
    needed = name_len + 1 + pub_len + 1;

    if (!family_name || *length < needed)
    {
        *length = needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    memcpy(family_name, name, name_len * sizeof(WCHAR));
    family_name[name_len] = L'_';
    memcpy(family_name + name_len + 1, publisher_id, (pub_len + 1) * sizeof(WCHAR));
    *length = needed;
    return ERROR_SUCCESS;
}

/***********************************************************************
 *         PackageFamilyNameFromId   (kernelbase.@)
 */
LONG WINAPI PackageFamilyNameFromId(const PACKAGE_ID *id, UINT32 *length, WCHAR *family_name)
{
    UINT32 name_len, pub_len, needed;

    TRACE("id %p, length %p, family_name %p.\n", id, length, family_name);

    if (!id || !id->name || !id->publisherId || !length)
        return ERROR_INVALID_PARAMETER;

    name_len = wcslen(id->name);
    pub_len = wcslen(id->publisherId);
    needed = name_len + 1 + pub_len + 1;

    if (!family_name || *length < needed)
    {
        *length = needed;
        return ERROR_INSUFFICIENT_BUFFER;
    }

    memcpy(family_name, id->name, name_len * sizeof(WCHAR));
    family_name[name_len] = L'_';
    memcpy(family_name + name_len + 1, id->publisherId, (pub_len + 1) * sizeof(WCHAR));
    *length = needed;
    return ERROR_SUCCESS;
}
