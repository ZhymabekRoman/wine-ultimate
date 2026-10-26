/*
 * Dynamic Hotpatching & Inline Trampoline Engine for Wine NTDLL
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "ntdll_misc.h"
#include "inline_patch.h"

#ifdef __x86_64__

extern void WINAPI fast_GetCurrentThreadId(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentThreadId,
                   "movl %gs:0x48,%eax\n\t"
                   "ret" )

extern void WINAPI fast_GetCurrentProcessId(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentProcessId,
                   "movl %gs:0x40,%eax\n\t"
                   "ret" )

extern void WINAPI fast_GetLastError(void);
__ASM_GLOBAL_FUNC( fast_GetLastError,
                   "movl %gs:0x68,%eax\n\t"
                   "ret" )

extern void WINAPI fast_SetLastError(DWORD err);
__ASM_GLOBAL_FUNC( fast_SetLastError,
                   "movl %ecx,%gs:0x68\n\t"
                   "ret" )

extern void WINAPI fast_GetProcessHeap(void);
__ASM_GLOBAL_FUNC( fast_GetProcessHeap,
                   "movq %gs:0x60,%rax\n\t"
                   "movq 0x30(%rax),%rax\n\t"
                   "ret" )

extern void WINAPI fast_NtCurrentTeb(void);
__ASM_GLOBAL_FUNC( fast_NtCurrentTeb,
                   "movq %gs:0x30,%rax\n\t"
                   "ret" )

static BOOL is_fast_trampoline_enabled(void)
{
    static int enabled = -1;
    if (enabled == -1)
    {
        UNICODE_STRING name = RTL_CONSTANT_STRING( L"WINE_NO_INLINE_PATCH" );
        UNICODE_STRING val;
        WCHAR buf[16];
        val.Buffer = buf;
        val.Length = 0;
        val.MaximumLength = sizeof(buf);

        enabled = (RtlQueryEnvironmentVariable_U( NULL, &name, &val ) == STATUS_SUCCESS) ? 0 : 1;
    }
    return enabled;
}

ULONG_PTR get_fast_iat_trampoline( const char *func_name )
{
    if (!is_fast_trampoline_enabled()) return 0;
    if (!func_name) return 0;

    switch (func_name[0])
    {
    case 'G':
        if (!strcmp( func_name, "GetCurrentThreadId" ))
            return (ULONG_PTR)fast_GetCurrentThreadId;
        if (!strcmp( func_name, "GetCurrentProcessId" ))
            return (ULONG_PTR)fast_GetCurrentProcessId;
        if (!strcmp( func_name, "GetLastError" ))
            return (ULONG_PTR)fast_GetLastError;
        if (!strcmp( func_name, "GetProcessHeap" ))
            return (ULONG_PTR)fast_GetProcessHeap;
        break;

    case 'S':
        if (!strcmp( func_name, "SetLastError" ))
            return (ULONG_PTR)fast_SetLastError;
        break;

    case 'R':
        if (!strcmp( func_name, "RtlGetLastWin32Error" ))
            return (ULONG_PTR)fast_GetLastError;
        if (!strcmp( func_name, "RtlSetLastWin32Error" ))
            return (ULONG_PTR)fast_SetLastError;
        break;

    case 'N':
        if (!strcmp( func_name, "NtCurrentTeb" ))
            return (ULONG_PTR)fast_NtCurrentTeb;
        break;
    }

    return 0;
}

BOOL is_module_eligible_for_inline_patch( const WCHAR *module_name, void *base )
{
    if (!is_fast_trampoline_enabled()) return FALSE;
    if (!module_name) return TRUE;

    /* Skip protected anti-cheat modules */
    if (wcsstr( module_name, L"easyanticheat" ) || wcsstr( module_name, L"battleye" ))
        return FALSE;

    return TRUE;
}

void patch_module_call_sites( void *module_base, SIZE_T image_size )
{
    /* Heuristic call site scanner can be expanded here */
}

#endif /* __x86_64__ */
