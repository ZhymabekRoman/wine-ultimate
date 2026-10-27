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

extern void WINAPI fast_GetCurrentProcess(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentProcess,
                   "movq $-1,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetCurrentThread(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentThread,
                   "movq $-2,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetTickCount(void);
__ASM_GLOBAL_FUNC( fast_GetTickCount,
                   "movl 0x7ffe0320,%eax\n\t"
                   "ret" )

extern ULONGLONG WINAPI fast_GetTickCount64(void);
__ASM_GLOBAL_FUNC( fast_GetTickCount64,
                   "1:\n\t"
                   "movl 0x7ffe0324,%edx\n\t"
                   "movl 0x7ffe0320,%eax\n\t"
                   "cmpl 0x7ffe0328,%edx\n\t"
                   "jne 1b\n\t"
                   "shlq $32,%rdx\n\t"
                   "orq %rdx,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetSystemTimeAsFileTime(void *time);
__ASM_GLOBAL_FUNC( fast_GetSystemTimeAsFileTime,
                   "1:\n\t"
                   "movl 0x7ffe0018,%edx\n\t"
                   "movl 0x7ffe0014,%eax\n\t"
                   "cmpl 0x7ffe001c,%edx\n\t"
                   "jne 1b\n\t"
                   "shlq $32,%rdx\n\t"
                   "orq %rdx,%rax\n\t"
                   "movq %rax,(%rcx)\n\t"
                   "ret" )

extern void * WINAPI fast_TlsGetValue(DWORD index);
__ASM_GLOBAL_FUNC( fast_TlsGetValue,
                   "cmpl $64,%ecx\n\t"
                   "jae 1f\n\t"
                   "movl %ecx,%eax\n\t"
                   "movq %gs:0x1480(,%rax,8),%rax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "cmpl $1088,%ecx\n\t"
                   "jae 2f\n\t"
                   "movq %gs:0x1780,%rax\n\t"
                   "testq %rax,%rax\n\t"
                   "jz 2f\n\t"
                   "movl %ecx,%edx\n\t"
                   "subl $64,%edx\n\t"
                   "movq (%rax,%rdx,8),%rax\n\t"
                   "ret\n\t"
                   "2:\n\t"
                   "xorq %rax,%rax\n\t"
                   "ret" )

BOOL WINAPI slow_TlsSetValueExpansion( DWORD index, void *val )
{
    TEB *teb = NtCurrentTeb();
    if (!teb->TlsExpansionSlots)
    {
        teb->TlsExpansionSlots = RtlAllocateHeap( teb->Peb->ProcessHeap, HEAP_ZERO_MEMORY, 1024 * sizeof(void *) );
        if (!teb->TlsExpansionSlots)
        {
            teb->LastErrorValue = 14; /* ERROR_NOT_ENOUGH_MEMORY */
            return FALSE;
        }
    }
    teb->TlsExpansionSlots[index - 64] = val;
    return TRUE;
}

extern BOOL WINAPI fast_TlsSetValue( DWORD index, void *val );
__ASM_GLOBAL_FUNC( fast_TlsSetValue,
                   "cmpl $64,%ecx\n\t"
                   "jae 1f\n\t"
                   "movl %ecx,%eax\n\t"
                   "movq %rdx,%gs:0x1480(,%rax,8)\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "cmpl $1088,%ecx\n\t"
                   "jae 3f\n\t"
                   "movq %gs:0x1780,%rax\n\t"
                   "testq %rax,%rax\n\t"
                   "jz 2f\n\t"
                   "movl %ecx,%r8d\n\t"
                   "subl $64,%r8d\n\t"
                   "movq %rdx,(%rax,%r8,8)\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "2:\n\t"
                   "jmp slow_TlsSetValueExpansion\n\t"
                   "3:\n\t"
                   "movl $87,%gs:0x68\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret" )

extern void WINAPI fast_EnterCriticalSection( RTL_CRITICAL_SECTION *crit );
__ASM_GLOBAL_FUNC( fast_EnterCriticalSection,
                   "movl $-1,%eax\n\t"
                   "xorl %edx,%edx\n\t"
                   "lock cmpxchgl %edx,0x8(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "movl %gs:0x48,%eax\n\t"
                   "movq %rax,0x10(%rcx)\n\t"
                   "movl $1,0xc(%rcx)\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlEnterCriticalSection" )

extern void WINAPI fast_LeaveCriticalSection( RTL_CRITICAL_SECTION *crit );
__ASM_GLOBAL_FUNC( fast_LeaveCriticalSection,
                   "cmpl $1,0xc(%rcx)\n\t"
                   "jne 1f\n\t"
                   "movq $0,0x10(%rcx)\n\t"
                   "movl $0,0xc(%rcx)\n\t"
                   "lock decl 0x8(%rcx)\n\t"
                   "js 2f\n\t"
                   "jmp RtlpUnWaitCriticalSection\n\t"
                   "2:\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlLeaveCriticalSection" )

extern DWORD WINAPI fast_RtlRunOnceExecuteOnce( RTL_RUN_ONCE *once, PRTL_RUN_ONCE_INIT_FN func, void *param, void **context );
__ASM_GLOBAL_FUNC( fast_RtlRunOnceExecuteOnce,
                   "movq (%rcx),%rax\n\t"
                   "testb $2,%al\n\t"
                   "jz 1f\n\t"
                   "testq %r9,%r9\n\t"
                   "jz 2f\n\t"
                   "andq $-4,%rax\n\t"
                   "movq %rax,(%r9)\n\t"
                   "2:\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlRunOnceExecuteOnce" )

static inline DWORD fnv1a_32( const char *str )
{
    DWORD hash = 2166136261u;
    while (*str)
    {
        hash ^= (BYTE)*str++;
        hash *= 16777619u;
    }
    return hash;
}

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
    DWORD hash;

    if (!is_fast_trampoline_enabled()) return 0;
    if (!func_name) return 0;

    hash = fnv1a_32( func_name );
    switch (hash)
    {
    case 0xebcdfe95u: if (!strcmp(func_name, "GetCurrentThreadId")) return (ULONG_PTR)fast_GetCurrentThreadId; break;
    case 0x020a0780u: if (!strcmp(func_name, "GetCurrentProcessId")) return (ULONG_PTR)fast_GetCurrentProcessId; break;
    case 0x6dd8a845u: if (!strcmp(func_name, "GetCurrentProcess")) return (ULONG_PTR)fast_GetCurrentProcess; break;
    case 0x9bc5a608u: if (!strcmp(func_name, "GetCurrentThread")) return (ULONG_PTR)fast_GetCurrentThread; break;
    case 0x5056df37u: if (!strcmp(func_name, "GetLastError")) return (ULONG_PTR)fast_GetLastError; break;
    case 0x86fe8652u: if (!strcmp(func_name, "GetProcessHeap")) return (ULONG_PTR)fast_GetProcessHeap; break;
    case 0x0f4d8b55u: if (!strcmp(func_name, "GetTickCount")) return (ULONG_PTR)fast_GetTickCount; break;
    case 0x18017917u: if (!strcmp(func_name, "GetTickCount64")) return (ULONG_PTR)fast_GetTickCount64; break;
    case 0xee9d4374u: if (!strcmp(func_name, "GetSystemTimeAsFileTime")) return (ULONG_PTR)fast_GetSystemTimeAsFileTime; break;
    case 0xccd84087u: if (!strcmp(func_name, "TlsGetValue")) return (ULONG_PTR)fast_TlsGetValue; break;
    case 0x7e0db2f3u: if (!strcmp(func_name, "TlsSetValue")) return (ULONG_PTR)fast_TlsSetValue; break;
    case 0x21ddcd8bu: if (!strcmp(func_name, "SetLastError")) return (ULONG_PTR)fast_SetLastError; break;
    case 0xb2524fccu: if (!strcmp(func_name, "RtlGetLastWin32Error")) return (ULONG_PTR)fast_GetLastError; break;
    case 0x9ebeaab8u: if (!strcmp(func_name, "RtlSetLastWin32Error")) return (ULONG_PTR)fast_SetLastError; break;
    case 0xa343d69au: if (!strcmp(func_name, "RtlGetProcessHeap")) return (ULONG_PTR)fast_GetProcessHeap; break;
    case 0x3f30200bu: if (!strcmp(func_name, "NtCurrentTeb")) return (ULONG_PTR)fast_NtCurrentTeb; break;
    case 0xe91b4077u: if (!strcmp(func_name, "EnterCriticalSection")) return (ULONG_PTR)fast_EnterCriticalSection; break;
    case 0xc70dd340u: if (!strcmp(func_name, "LeaveCriticalSection")) return (ULONG_PTR)fast_LeaveCriticalSection; break;
    case 0x97c0ce0fu: if (!strcmp(func_name, "RtlEnterCriticalSection")) return (ULONG_PTR)fast_EnterCriticalSection; break;
    case 0xaf2a3c58u: if (!strcmp(func_name, "RtlLeaveCriticalSection")) return (ULONG_PTR)fast_LeaveCriticalSection; break;
    case 0xc6be3f6fu: if (!strcmp(func_name, "RtlRunOnceExecuteOnce")) return (ULONG_PTR)fast_RtlRunOnceExecuteOnce; break;
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
