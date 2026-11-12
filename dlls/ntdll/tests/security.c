/*
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

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include <winternl.h>

#include "wine/test.h"

static NTSTATUS (WINAPI *pNtQueryInformationToken)(HANDLE, TOKEN_INFORMATION_CLASS, void *, ULONG, ULONG *);
static NTSTATUS (WINAPI *pNtSetInformationToken)(HANDLE, TOKEN_INFORMATION_CLASS, void *, ULONG);
static NTSTATUS (WINAPI *pNtCreateLowBoxToken)(HANDLE *, HANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES *,
                                               SID *, ULONG, SID_AND_ATTRIBUTES *, ULONG, HANDLE *);

static const SID_IDENTIFIER_AUTHORITY mandatory_authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
static const SID_IDENTIFIER_AUTHORITY appcontainer_authority = SECURITY_APP_PACKAGE_AUTHORITY;

static SID *alloc_sid( const SID_IDENTIFIER_AUTHORITY *authority, DWORD count, const DWORD *rids )
{
    SID *sid = HeapAlloc( GetProcessHeap(), 0, FIELD_OFFSET( SID, SubAuthority[count] ));

    sid->Revision = SID_REVISION;
    sid->SubAuthorityCount = count;
    sid->IdentifierAuthority = *authority;
    for (DWORD i = 0; i < count; i++) sid->SubAuthority[i] = rids[i];
    return sid;
}

static void test_integrity_level( void )
{
    static const DWORD low_rid = SECURITY_MANDATORY_LOW_RID;
    TOKEN_MANDATORY_LABEL *tml;
    HANDLE token;
    PSID sid;
    ULONG size;
    NTSTATUS status;

    ok( OpenProcessToken( GetCurrentProcess(), TOKEN_ALL_ACCESS, &token ), "Failed to open token\n" );

    size = sizeof(*tml) + sizeof(SID) + sizeof(DWORD);
    tml = HeapAlloc( GetProcessHeap(), 0, size );

    status = pNtQueryInformationToken( token, TokenIntegrityLevel, tml, size, &size );
    ok( !status, "NtQueryInformationToken failed, status %#lx\n", status );
    {
        const SID *label = tml->Label.Sid;

        ok( label->Revision == SID_REVISION, "got revision %u\n", label->Revision );
        ok( label->SubAuthorityCount == 1, "got %u subauthorities\n", label->SubAuthorityCount );
        ok( !memcmp( &label->IdentifierAuthority, &mandatory_authority,
                     sizeof(label->IdentifierAuthority) ), "wrong integrity authority\n" );
        ok( label->SubAuthority[0] == SECURITY_MANDATORY_HIGH_RID,
            "expected high integrity, got %#x\n", label->SubAuthority[0] );
    }
    ok( tml->Label.Attributes & SE_GROUP_INTEGRITY, "missing SE_GROUP_INTEGRITY\n" );

    /* set the integrity level to low and read it back */
    sid = alloc_sid( &mandatory_authority, 1, &low_rid );
    {
        TOKEN_MANDATORY_LABEL label;

        label.Label.Sid = sid;
        label.Label.Attributes = SE_GROUP_INTEGRITY;
        status = pNtSetInformationToken( token, TokenIntegrityLevel, &label, sizeof(label) );
        ok( !status, "NtSetInformationToken failed, status %#lx\n", status );
    }

    status = pNtQueryInformationToken( token, TokenIntegrityLevel, tml, size, &size );
    ok( !status, "NtQueryInformationToken failed, status %#lx\n", status );
    {
        const SID *label = tml->Label.Sid;
        ok( label->SubAuthority[0] == SECURITY_MANDATORY_LOW_RID,
            "expected low integrity, got %#x\n", label->SubAuthority[0] );
    }

    HeapFree( GetProcessHeap(), 0, tml );
    HeapFree( GetProcessHeap(), 0, sid );
    CloseHandle( token );
}

static void test_appcontainer( void )
{
    static const DWORD app_rid = 0x00001234;
    static const DWORD lc_rid = 0x00112233;
    static const DWORD cap0_rid = 0x00005678;
    static const DWORD cap1_rid = 0x00009abc;
    TOKEN_APPCONTAINER_INFORMATION *ac;
    SID_AND_ATTRIBUTES caps[2];
    TOKEN_GROUPS *groups;
    HANDLE token, lowbox = NULL;
    PSID app_sid, cap0, cap1;
    ULONG size;
    DWORD is_appcontainer, appcontainer_num;
    NTSTATUS status;

    ok( OpenProcessToken( GetCurrentProcess(), TOKEN_ALL_ACCESS, &token ), "Failed to open token\n" );

    /* a regular token is not an appcontainer */
    is_appcontainer = 1;
    status = pNtQueryInformationToken( token, TokenIsAppContainer, &is_appcontainer,
                                       sizeof(is_appcontainer), &size );
    ok( !status, "NtQueryInformationToken(TokenIsAppContainer) failed, status %#lx\n", status );
    ok( is_appcontainer == 0, "expected a non-appcontainer token\n" );

    ac = HeapAlloc( GetProcessHeap(), 0, sizeof(*ac) + sizeof(SID) + sizeof(DWORD) );
    status = pNtQueryInformationToken( token, TokenAppContainerSid, ac,
                                       sizeof(*ac) + sizeof(SID) + sizeof(DWORD), &size );
    ok( !status, "NtQueryInformationToken(TokenAppContainerSid) failed, status %#lx\n", status );
    ok( ac->TokenAppContainer == NULL, "expected no appcontainer sid\n" );
    HeapFree( GetProcessHeap(), 0, ac );

    /* create the appcontainer token */
    app_sid = alloc_sid( &appcontainer_authority, 2, (const DWORD[]){ app_rid, lc_rid } );
    cap0 = alloc_sid( &appcontainer_authority, 2, (const DWORD[]){ cap0_rid, lc_rid } );
    cap1 = alloc_sid( &appcontainer_authority, 2, (const DWORD[]){ cap1_rid, lc_rid } );
    caps[0].Sid = cap0;
    caps[0].Attributes = 0;
    caps[1].Sid = cap1;
    caps[1].Attributes = 0;

    status = pNtCreateLowBoxToken( &lowbox, token, TOKEN_ALL_ACCESS, NULL, app_sid, 2, caps, 0, NULL );
    ok( !status, "NtCreateLowBoxToken failed, status %#lx\n", status );
    ok( lowbox != NULL, "got no lowbox token handle\n" );
    if (!lowbox) goto done;

    is_appcontainer = 0;
    status = pNtQueryInformationToken( lowbox, TokenIsAppContainer, &is_appcontainer,
                                       sizeof(is_appcontainer), &size );
    ok( !status, "NtQueryInformationToken(TokenIsAppContainer) failed, status %#lx\n", status );
    ok( is_appcontainer == 1, "expected an appcontainer token\n" );

    appcontainer_num = 0;
    status = pNtQueryInformationToken( lowbox, TokenAppContainerNumber, &appcontainer_num,
                                       sizeof(appcontainer_num), &size );
    ok( !status, "NtQueryInformationToken(TokenAppContainerNumber) failed, status %#lx\n", status );
    ok( appcontainer_num == lc_rid, "expected lc number %#x, got %#x\n", lc_rid, appcontainer_num );

    size = sizeof(*ac) + sizeof(SID) + sizeof(DWORD);
    status = pNtQueryInformationToken( lowbox, TokenAppContainerSid, ac, size, &size );
    ok( !status, "NtQueryInformationToken(TokenAppContainerSid) failed, status %#lx\n", status );
    ok( ac->TokenAppContainer != NULL, "expected an appcontainer sid\n" );
    if (ac->TokenAppContainer)
        ok( EqualSid( ac->TokenAppContainer, app_sid ), "got a different appcontainer sid\n" );

    /* integrity level of a lowbox token is low */
    size = sizeof(TOKEN_MANDATORY_LABEL) + sizeof(SID) + sizeof(DWORD);
    {
        TOKEN_MANDATORY_LABEL *tml = (void *)HeapAlloc( GetProcessHeap(), 0, size );

        status = pNtQueryInformationToken( lowbox, TokenIntegrityLevel, tml, size, &size );
        ok( !status, "NtQueryInformationToken(TokenIntegrityLevel) failed, status %#lx\n", status );
        {
            const SID *label = tml->Label.Sid;
            ok( label->SubAuthority[0] == SECURITY_MANDATORY_LOW_RID,
                "expected low integrity, got %#x\n", label->SubAuthority[0] );
        }
        HeapFree( GetProcessHeap(), 0, tml );
    }

    /* capabilities round trip */
    size = FIELD_OFFSET( TOKEN_GROUPS, Groups[2] ) + 2 * (sizeof(SID) + sizeof(DWORD));
    groups = HeapAlloc( GetProcessHeap(), 0, size );
    status = pNtQueryInformationToken( lowbox, TokenCapabilities, groups, size, &size );
    ok( !status, "NtQueryInformationToken(TokenCapabilities) failed, status %#lx\n", status );
    ok( groups->GroupCount == 2, "got %u capabilities\n", (unsigned int)groups->GroupCount );
    if (groups->GroupCount == 2)
    {
        ok( EqualSid( groups->Groups[0].Sid, cap0 ), "got a wrong first capability\n" );
        ok( EqualSid( groups->Groups[1].Sid, cap1 ), "got a wrong second capability\n" );
        ok( groups->Groups[0].Attributes == 0, "got attrs %#x\n", (unsigned int)groups->Groups[0].Attributes );
    }
    HeapFree( GetProcessHeap(), 0, groups );

    CloseHandle( lowbox );

done:
    HeapFree( GetProcessHeap(), 0, ac );
    HeapFree( GetProcessHeap(), 0, app_sid );
    HeapFree( GetProcessHeap(), 0, cap0 );
    HeapFree( GetProcessHeap(), 0, cap1 );
    CloseHandle( token );
}

START_TEST( security )
{
    HMODULE mod = GetModuleHandleA( "ntdll.dll" );

    pNtQueryInformationToken = (void *)GetProcAddress( mod, "NtQueryInformationToken" );
    pNtSetInformationToken = (void *)GetProcAddress( mod, "NtSetInformationToken" );
    pNtCreateLowBoxToken = (void *)GetProcAddress( mod, "NtCreateLowBoxToken" );
    if (!pNtCreateLowBoxToken)
    {
        win_skip( "NtCreateLowBoxToken is not available\n" );
        return;
    }

    test_integrity_level();
    test_appcontainer();
}
