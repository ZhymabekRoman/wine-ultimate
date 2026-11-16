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
#include <stdint.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "ntdll_misc.h"
#include "inline_patch.h"

#ifdef __x86_64__

#include <emmintrin.h>  /* SSE2 probe used by fast_RtlInitUnicodeString and fast_RtlEqualUnicodeString */

/* L1i / BTB entry alignment for the hot trampolines.
 *
 * include/wine/asm.h sets __ASM_FUNC_ALIGN to ".align 4" for x86_64, which is a
 * 4-byte boundary in GAS x86 syntax, so every trampoline entry point used to land
 * wherever it fell and frequently straddled a 32-byte i-cache line / BTB tag.
 * Overriding the macro aligns the label itself (emitted before .globl) onto a
 * 16-byte boundary and fills the gap with 0x90 nops, so an indirect call through a
 * patched IAT slot always fetches a trampoline that starts on a line boundary.
 *
 * Each trampoline body additionally opens with ".p2align 4, 0x90": with the macro
 * override in place the position is already aligned, so that directive assembles to
 * zero padding bytes today and keeps the guarantee if a body ever begins with its
 * own label. */
#undef __ASM_FUNC_ALIGN
#define __ASM_FUNC_ALIGN ".p2align 4, 0x90"

extern void WINAPI fast_GetCurrentThreadId(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentThreadId,
                   ".p2align 4, 0x90\n\t"
                   "movl %gs:0x48,%eax\n\t"
                   "ret" )

extern void WINAPI fast_GetCurrentProcessId(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentProcessId,
                   ".p2align 4, 0x90\n\t"
                   "movl %gs:0x40,%eax\n\t"
                   "ret" )

extern void WINAPI fast_GetLastError(void);
__ASM_GLOBAL_FUNC( fast_GetLastError,
                   ".p2align 4, 0x90\n\t"
                   "movl %gs:0x68,%eax\n\t"
                   "ret" )

extern void WINAPI fast_SetLastError(DWORD err);
__ASM_GLOBAL_FUNC( fast_SetLastError,
                   ".p2align 4, 0x90\n\t"
                   "movl %ecx,%gs:0x68\n\t"
                   "ret" )

extern void WINAPI fast_GetProcessHeap(void);
__ASM_GLOBAL_FUNC( fast_GetProcessHeap,
                   ".p2align 4, 0x90\n\t"
                   "movq %gs:0x60,%rax\n\t"
                   "movq 0x30(%rax),%rax\n\t"
                   "ret" )

extern void WINAPI fast_NtCurrentTeb(void);
__ASM_GLOBAL_FUNC( fast_NtCurrentTeb,
                   ".p2align 4, 0x90\n\t"
                   "movq %gs:0x30,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetCurrentProcess(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentProcess,
                   ".p2align 4, 0x90\n\t"
                   "movq $-1,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetCurrentThread(void);
__ASM_GLOBAL_FUNC( fast_GetCurrentThread,
                   ".p2align 4, 0x90\n\t"
                   "movq $-2,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetTickCount(void);
__ASM_GLOBAL_FUNC( fast_GetTickCount,
                   ".p2align 4, 0x90\n\t"
                   "movl 0x7ffe0320,%eax\n\t"
                   "ret" )

extern ULONGLONG WINAPI fast_GetTickCount64(void);
__ASM_GLOBAL_FUNC( fast_GetTickCount64,
                   ".p2align 4, 0x90\n\t"
                   "movq 0x7ffe0320,%rax\n\t"
                   "ret" )

extern void WINAPI fast_GetSystemTimeAsFileTime(void *time);
__ASM_GLOBAL_FUNC( fast_GetSystemTimeAsFileTime,
                   ".p2align 4, 0x90\n\t"
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
                   ".p2align 4, 0x90\n\t"
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
                   ".p2align 4, 0x90\n\t"
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
                   ".p2align 4, 0x90\n\t"
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
                   ".p2align 4, 0x90\n\t"
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
                   ".p2align 4, 0x90\n\t"
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

extern BOOL WINAPI fast_IsProcessorFeaturePresent( DWORD feature );
__ASM_GLOBAL_FUNC( fast_IsProcessorFeaturePresent,
                   ".p2align 4, 0x90\n\t"
                   "cmpl $64,%ecx\n\t"
                   "jae 1f\n\t"
                   "movzbl 0x7ffe0274(%rcx),%eax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret" )

extern BOOL WINAPI fast_QueryPerformanceCounter( LARGE_INTEGER *counter );
__ASM_GLOBAL_FUNC( fast_QueryPerformanceCounter,
                   ".p2align 4, 0x90\n\t"
                   "testq %rcx,%rcx\n\t"
                   "jz 1f\n\t"
                   "testb $1,0x7ffe03c6\n\t"
                   "jz 2f\n\t"
                   "movq %rcx,%r8\n\t"
                   "testb $2,0x7ffe03c6\n\t"
                   "jz 3f\n\t"
                   "rdtscp\n\t"
                   "jmp 4f\n\t"
                   "3:\n\t"
                   "testb $4,0x7ffe03c6\n\t"
                   "jz 5f\n\t"
                   "lfence\n\t"
                   "5:\n\t"
                   "rdtsc\n\t"
                   "4:\n\t"
                   "shlq $32,%rdx\n\t"
                   "orq %rdx,%rax\n\t"
                   "addq 0x7ffe03b8,%rax\n\t"
                   "movzbl 0x7ffe03c7,%ecx\n\t"
                   "shrq %cl,%rax\n\t"
                   "movq %rax,(%r8)\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "2:\n\t"
                   "jmp RtlQueryPerformanceCounter\n\t"
                   "1:\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret" )

/* QueryPerformanceFrequency / RtlQueryPerformanceFrequency: the frequency is a
   constant, published by wineboot into KUSER_SHARED_DATA->QpcFrequency (0x300)
   during QPC calibration. ntdll's RtlQueryPerformanceFrequency only trusts that
   field when QpcBypassEnabled (0x3c6) carries SHARED_GLOBAL_FLAGS_QPC_BYPASS_ENABLED
   and otherwise reports TICKSPERSEC, so replicate that test: reading the field
   unconditionally would hand out 0 (division by zero) on a page that has not been
   calibrated yet. */
extern BOOL WINAPI fast_QueryPerformanceFrequency( LARGE_INTEGER *frequency );
__ASM_GLOBAL_FUNC( fast_QueryPerformanceFrequency,
                   ".p2align 4, 0x90\n\t"
                   "testq %rcx,%rcx\n\t"
                   "jz 1f\n\t"
                   "testb $1,0x7ffe03c6\n\t"
                   "jz 2f\n\t"
                   "movq 0x7ffe0300,%rax\n\t"
                   "movq %rax,(%rcx)\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "2:\n\t"
                   "movq $10000000,%rax\n\t"     /* TICKSPERSEC */
                   "movq %rax,(%rcx)\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret" )

/* GetSystemTimePreciseAsFileTime: same seqlock read of
   KUSER_SHARED_DATA->SystemTime (0x14) as fast_GetSystemTimeAsFileTime, plus the
   NULL check the real entry point gets for free. */
extern void WINAPI fast_GetSystemTimePreciseAsFileTime( void *time );
__ASM_GLOBAL_FUNC( fast_GetSystemTimePreciseAsFileTime,
                   ".p2align 4, 0x90\n\t"
                   "testq %rcx,%rcx\n\t"
                   "jz 2f\n\t"
                   "1:\n\t"
                   "movl 0x7ffe0018,%edx\n\t"
                   "movl 0x7ffe0014,%eax\n\t"
                   "cmpl 0x7ffe001c,%edx\n\t"
                   "jne 1b\n\t"
                   "shlq $32,%rdx\n\t"
                   "orq %rdx,%rax\n\t"
                   "movq %rax,(%rcx)\n\t"
                   "2:\n\t"
                   "ret" )

/* Fast inline IAT trampoline for RtlInitUnicodeString.
 *
 * RtlInitUnicodeString is one of the hottest calls made into ntdll: every
 * UNICODE_STRING built by a builtin for an API call (directory, key and
 * object names) runs the generic strlenW + field fill in rtlstr.c. The fast
 * path here answers every string of up to 7 wide characters from a single
 * unaligned 16-byte SSE2 probe: the terminator is located with
 * _mm_cmpeq_epi16 against zero plus a movemask, so the byte length is the
 * trailing-zero count of the mask (two mask bits per wide character).
 *
 * The 16-byte probe is only taken when it stays inside the caller's 4K page.
 * A source string is not owned by us, and an unaligned load that reaches into
 * the next page would fault when that page is unmapped (a string placed at
 * the tail of a page by VirtualAlloc is a real case), so page-crossing probes
 * go straight to the generic implementation.
 *
 * Everything the fast path writes is exactly what RtlInitUnicodeString
 * writes: Buffer aliases the source, Length is the byte length without the
 * terminator, MaximumLength adds the terminator. Longer strings (and
 * page-crossing ones) tail-call the real implementation, which is ABI
 * identical, so the trampoline is transparent to the caller.
 */
void WINAPI fast_RtlInitUnicodeString( PUNICODE_STRING target, PCWSTR source )
{
    if (!target) return;

    if (!source)
    {
        target->Buffer = NULL;
        target->Length = 0;
        target->MaximumLength = 0;
        return;
    }

    target->Buffer = (PWSTR)source;

    if (((ULONG_PTR)source & 0xfff) <= 0xff0)  /* the 16-byte probe is page-contained */
    {
        __m128i chunk = _mm_loadu_si128( (const __m128i *)source );
        int mask = _mm_movemask_epi8( _mm_cmpeq_epi16( chunk, _mm_setzero_si128() ));

        if (mask)
        {
            USHORT len = (USHORT)__builtin_ctz( (unsigned int)mask );  /* bytes, no terminator */
            target->Length = len;
            target->MaximumLength = len + 2;  /* + terminating NUL */
            return;
        }
    }

    RtlInitUnicodeString( target, source );
}

/* GetVersion: KUSER_SHARED_DATA publishes the version that wineboot writes into
   the shared user page - NtMajorVersion (0x26c), NtMinorVersion (0x270) and
   NtBuildNumber (0x260), offsets per include/ddk/wdm.h. Packed per the Win32
   contract: (Major & 0xff) | ((Minor & 0xff) << 8) | ((Build & 0x7fff) << 16).
   The shared page is mapped and filled before any application code runs
   (programs/wineboot/wineboot.c), so no seqlock retry loop is needed: these
   three fields are written once and never updated. */
extern DWORD WINAPI fast_GetVersion(void);
__ASM_GLOBAL_FUNC( fast_GetVersion,
                   ".p2align 4, 0x90\n\t"
                   "movl 0x7ffe026c,%eax\n\t"
                   "movzbl %al,%eax\n\t"
                   "movl 0x7ffe0270,%edx\n\t"
                   "movzbl %dl,%edx\n\t"
                   "shll $8,%edx\n\t"
                   "orl %edx,%eax\n\t"
                   "movl 0x7ffe0260,%edx\n\t"
                   "andl $0x7fff,%edx\n\t"
                   "shll $16,%edx\n\t"
                   "orl %edx,%eax\n\t"
                   "ret" )

/* GetActiveProcessorCount: single-group Wine publishes the active CPU count once
   into KUSER_SHARED_DATA->ActiveProcessorCount (dlls/ntdll/unix/virtual.c
   init_shared_data sets it from peb->NumberOfProcessors). include/ddk/wdm.h puts
   that field at 0x3c0 (0x2ec is SafeBootMode), so the load reads 0x7ffe03c0.
   The WORD group is zero-extended before the compare because the Win64 ABI leaves
   the upper 16 bits of %ecx undefined for a 16-bit argument: ALL_PROCESSOR_GROUPS
   and group 0 both take the count (Wine has exactly one group), every other group
   is out of range and returns 0, matching the kernel32 fallback. */
extern DWORD WINAPI fast_GetActiveProcessorCount( WORD group );
__ASM_GLOBAL_FUNC( fast_GetActiveProcessorCount,
                   ".p2align 4, 0x90\n\t"
                   "movzwl %cx,%ecx\n\t"
                   "cmpl $0xffff,%ecx\n\t"
                   "je 1f\n\t"
                   "testl %ecx,%ecx\n\t"
                   "jnz 2f\n\t"
                   "1:\n\t"
                   "movl 0x7ffe03c0,%eax\n\t"
                   "ret\n\t"
                   "2:\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret" )

/* RtlEqualUnicodeString: SSE2 fast path for the dominant case - case-sensitive
   comparison of a short string. UNICODE_STRING is { Length (0), MaximumLength (2),
   Buffer (8) }. Dispatch keeps every read inside the declared Length: the 2/4/8/16
   byte loads are exact-size, so a 16-byte movdqu is only issued when Length == 16
   and MaximumLength >= Length guarantees the bytes are mapped. Lengths that are
   not an exact load width (1,3,5,6,7,9..15) and every length above 16 fall through
   to the C implementation, as does the whole case-insensitive path, which needs
   RtlCompareUnicodeString. rcx/rdx/r8 stay untouched so the tail jump re-enters
   with the original arguments; rax,r9,r10,r11 and xmm0/xmm1 are all caller-saved. */
extern BOOLEAN WINAPI fast_RtlEqualUnicodeString( const UNICODE_STRING *s1, const UNICODE_STRING *s2,
                                                  BOOLEAN case_in_sensitive );
__ASM_GLOBAL_FUNC( fast_RtlEqualUnicodeString,
                   ".p2align 4, 0x90\n\t"
                   "cmpq %rcx,%rdx\n\t"          /* s1 == s2 (includes both NULL) */
                   "je 5f\n\t"
                   "testq %rcx,%rcx\n\t"
                   "jz 6f\n\t"
                   "testq %rdx,%rdx\n\t"
                   "jz 6f\n\t"
                   "movzwl (%rcx),%r9d\n\t"      /* s1->Length */
                   "cmpw (%rdx),%r9w\n\t"        /* != s2->Length */
                   "jne 6f\n\t"
                   "testb %r8b,%r8b\n\t"
                   "jnz 7f\n\t"                  /* case-insensitive -> C impl */
                   "testw %r9w,%r9w\n\t"
                   "jz 5f\n\t"                   /* Length == 0 -> equal */
                   "cmpw $16,%r9w\n\t"
                   "ja 7f\n\t"                   /* > 16 bytes -> C impl */
                   "movq 8(%rcx),%rax\n\t"       /* s1->Buffer */
                   "movq 8(%rdx),%r11\n\t"       /* s2->Buffer */
                   "cmpq %rax,%r11\n\t"
                   "je 5f\n\t"                   /* same buffer -> equal */
                   "testq %rax,%rax\n\t"
                   "jz 6f\n\t"
                   "testq %r11,%r11\n\t"
                   "jz 6f\n\t"
                   "cmpw $2,%r9w\n\t"
                   "je 2f\n\t"
                   "cmpw $4,%r9w\n\t"
                   "je 4f\n\t"
                   "cmpw $8,%r9w\n\t"
                   "je 8f\n\t"
                   "cmpw $16,%r9w\n\t"
                   "je 16f\n\t"
                   "jmp 7f\n\t"                  /* odd/intermediate length -> C impl */
                   "2:\n\t"
                   "movzwl (%rax),%r10d\n\t"
                   "cmpw (%r11),%r10w\n\t"
                   "sete %al\n\t"
                   "movzbl %al,%eax\n\t"
                   "ret\n\t"
                   "4:\n\t"
                   "movl (%rax),%r10d\n\t"
                   "cmpl (%r11),%r10d\n\t"
                   "sete %al\n\t"
                   "movzbl %al,%eax\n\t"
                   "ret\n\t"
                   "8:\n\t"
                   "movq (%rax),%r10\n\t"
                   "cmpq (%r11),%r10\n\t"
                   "sete %al\n\t"
                   "movzbl %al,%eax\n\t"
                   "ret\n\t"
                   "16:\n\t"
                   "movdqu (%rax),%xmm0\n\t"
                   "movdqu (%r11),%xmm1\n\t"
                   "pcmpeqb %xmm1,%xmm0\n\t"
                   "pmovmskb %xmm0,%eax\n\t"
                   "cmpl $0xffff,%eax\n\t"
                   "sete %al\n\t"
                   "movzbl %al,%eax\n\t"
                   "ret\n\t"
                   "5:\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "6:\n\t"
                   "xorl %eax,%eax\n\t"
                   "ret\n\t"
                   "7:\n\t"
                   "jmp RtlEqualUnicodeString" )

/* SRWLock fast paths matching struct srw_lock in dlls/ntdll/sync.c:
 *   offset 0 (low 16 bits):  short exclusive_waiters (bit 0 = held exclusive, bits 1..15 = waiter count)
 *   offset 2 (high 16 bits): unsigned short owners
 * Combined as a 32-bit LONG at (%rcx):
 *   0x00000000 = unlocked
 *   0x00010001 = held exclusive, 0 waiters
 *   0xNNNN0000 = held shared by NNNN owners, 0 exclusive waiters
 */
extern void WINAPI fast_RtlAcquireSRWLockExclusive( RTL_SRWLOCK *lock );
__ASM_GLOBAL_FUNC( fast_RtlAcquireSRWLockExclusive,
                   ".p2align 4, 0x90\n\t"
                   "xorl %eax,%eax\n\t"
                   "movl $0x00010001,%edx\n\t"
                   "lock cmpxchgl %edx,(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlAcquireSRWLockExclusive" )

extern BOOLEAN WINAPI fast_RtlTryAcquireSRWLockExclusive( RTL_SRWLOCK *lock );
__ASM_GLOBAL_FUNC( fast_RtlTryAcquireSRWLockExclusive,
                   ".p2align 4, 0x90\n\t"
                   "xorl %eax,%eax\n\t"
                   "movl $0x00010001,%edx\n\t"
                   "lock cmpxchgl %edx,(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlTryAcquireSRWLockExclusive" )

extern void WINAPI fast_RtlReleaseSRWLockExclusive( RTL_SRWLOCK *lock );
__ASM_GLOBAL_FUNC( fast_RtlReleaseSRWLockExclusive,
                   ".p2align 4, 0x90\n\t"
                   "movl $0x00010001,%eax\n\t"
                   "xorl %edx,%edx\n\t"
                   "lock cmpxchgl %edx,(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "jmp RtlWakeAddressAll\n\t"
                   "1:\n\t"
                   "jmp RtlReleaseSRWLockExclusive" )

extern void WINAPI fast_RtlAcquireSRWLockShared( RTL_SRWLOCK *lock );
__ASM_GLOBAL_FUNC( fast_RtlAcquireSRWLockShared,
                   ".p2align 4, 0x90\n\t"
                   "movl (%rcx),%eax\n\t"
                   "testw %ax,%ax\n\t"
                   "jnz 1f\n\t"
                   "cmpl $0xffff0000,%eax\n\t"
                   "jae 1f\n\t"
                   "leal 0x10000(%rax),%edx\n\t"
                   "lock cmpxchgl %edx,(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlAcquireSRWLockShared" )

extern BOOLEAN WINAPI fast_RtlTryAcquireSRWLockShared( RTL_SRWLOCK *lock );
__ASM_GLOBAL_FUNC( fast_RtlTryAcquireSRWLockShared,
                   ".p2align 4, 0x90\n\t"
                   "movl (%rcx),%eax\n\t"
                   "testw %ax,%ax\n\t"
                   "jnz 1f\n\t"
                   "cmpl $0xffff0000,%eax\n\t"
                   "jae 1f\n\t"
                   "leal 0x10000(%rax),%edx\n\t"
                   "lock cmpxchgl %edx,(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "movl $1,%eax\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlTryAcquireSRWLockShared" )

extern void WINAPI fast_RtlReleaseSRWLockShared( RTL_SRWLOCK *lock );
__ASM_GLOBAL_FUNC( fast_RtlReleaseSRWLockShared,
                   ".p2align 4, 0x90\n\t"
                   "movl $0x00010000,%eax\n\t"
                   "xorl %edx,%edx\n\t"
                   "lock cmpxchgl %edx,(%rcx)\n\t"
                   "jnz 1f\n\t"
                   "ret\n\t"
                   "1:\n\t"
                   "jmp RtlReleaseSRWLockShared" )

extern PVOID WINAPI fast_EncodePointer( PVOID ptr );
__ASM_GLOBAL_FUNC( fast_EncodePointer,
                   ".p2align 4, 0x90\n\t"
                   "jmp RtlEncodePointer" )

extern PVOID WINAPI fast_DecodePointer( PVOID ptr );
__ASM_GLOBAL_FUNC( fast_DecodePointer,
                   ".p2align 4, 0x90\n\t"
                   "jmp RtlDecodePointer" )

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
    case 0x65c1bfcbu: if (!strcmp(func_name, "GetSystemTimePreciseAsFileTime")) return (ULONG_PTR)fast_GetSystemTimePreciseAsFileTime; break;
    case 0x27c5f681u: if (!strcmp(func_name, "QueryPerformanceCounter")) return (ULONG_PTR)fast_QueryPerformanceCounter; break;
    case 0x4c61e9b9u: if (!strcmp(func_name, "RtlQueryPerformanceCounter")) return (ULONG_PTR)fast_QueryPerformanceCounter; break;
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
    case 0x7aa3f32au: if (!strcmp(func_name, "IsProcessorFeaturePresent")) return (ULONG_PTR)fast_IsProcessorFeaturePresent; break;
    case 0xd1c44ba2u: if (!strcmp(func_name, "RtlIsProcessorFeaturePresent")) return (ULONG_PTR)fast_IsProcessorFeaturePresent; break;
    case 0x3376500fu: if (!strcmp(func_name, "RtlInitUnicodeString")) return (ULONG_PTR)fast_RtlInitUnicodeString; break;
    case 0xfdd349f7u: if (!strcmp(func_name, "QueryPerformanceFrequency")) return (ULONG_PTR)fast_QueryPerformanceFrequency; break;
    case 0xc05d1acfu: if (!strcmp(func_name, "RtlQueryPerformanceFrequency")) return (ULONG_PTR)fast_QueryPerformanceFrequency; break;
    /* (GetSystemTimePreciseAsFileTime: see the note on GetSystemTimeAsFileTime above.) */
    case 0x3a2f5799u: if (!strcmp(func_name, "GetVersion")) return (ULONG_PTR)fast_GetVersion; break;
    case 0x647a8efau: if (!strcmp(func_name, "GetActiveProcessorCount")) return (ULONG_PTR)fast_GetActiveProcessorCount; break;
    case 0x8af4c873u: if (!strcmp(func_name, "RtlEqualUnicodeString")) return (ULONG_PTR)fast_RtlEqualUnicodeString; break;
    case 0xda2f90e4u: if (!strcmp(func_name, "AcquireSRWLockExclusive")) return (ULONG_PTR)fast_RtlAcquireSRWLockExclusive; break;
    case 0x7875e76cu: if (!strcmp(func_name, "RtlAcquireSRWLockExclusive")) return (ULONG_PTR)fast_RtlAcquireSRWLockExclusive; break;
    case 0xcff2b985u: if (!strcmp(func_name, "TryAcquireSRWLockExclusive")) return (ULONG_PTR)fast_RtlTryAcquireSRWLockExclusive; break;
    case 0x5b8ae7edu: if (!strcmp(func_name, "RtlTryAcquireSRWLockExclusive")) return (ULONG_PTR)fast_RtlTryAcquireSRWLockExclusive; break;
    case 0x5c87aa4du: if (!strcmp(func_name, "ReleaseSRWLockExclusive")) return (ULONG_PTR)fast_RtlReleaseSRWLockExclusive; break;
    case 0x38ecb9d5u: if (!strcmp(func_name, "RtlReleaseSRWLockExclusive")) return (ULONG_PTR)fast_RtlReleaseSRWLockExclusive; break;
    case 0x84885029u: if (!strcmp(func_name, "AcquireSRWLockShared")) return (ULONG_PTR)fast_RtlAcquireSRWLockShared; break;
    case 0x597c45c1u: if (!strcmp(func_name, "RtlAcquireSRWLockShared")) return (ULONG_PTR)fast_RtlAcquireSRWLockShared; break;
    case 0xe99fa9eeu: if (!strcmp(func_name, "TryAcquireSRWLockShared")) return (ULONG_PTR)fast_RtlTryAcquireSRWLockShared; break;
    case 0xb6e7b846u: if (!strcmp(func_name, "RtlTryAcquireSRWLockShared")) return (ULONG_PTR)fast_RtlTryAcquireSRWLockShared; break;
    case 0x4f0e1426u: if (!strcmp(func_name, "ReleaseSRWLockShared")) return (ULONG_PTR)fast_RtlReleaseSRWLockShared; break;
    case 0x2da46ddeu: if (!strcmp(func_name, "RtlReleaseSRWLockShared")) return (ULONG_PTR)fast_RtlReleaseSRWLockShared; break;
    case 0x83ff1254u: if (!strcmp(func_name, "EncodePointer")) return (ULONG_PTR)fast_EncodePointer; break;
    case 0x982524fcu: if (!strcmp(func_name, "RtlEncodePointer")) return (ULONG_PTR)fast_EncodePointer; break;
    case 0x91125e9cu: if (!strcmp(func_name, "DecodePointer")) return (ULONG_PTR)fast_DecodePointer; break;
    case 0x5206e494u: if (!strcmp(func_name, "RtlDecodePointer")) return (ULONG_PTR)fast_DecodePointer; break;
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

static BOOL is_fast_trampoline_addr( ULONG_PTR addr )
{
    static const ULONG_PTR tramps[] = {
        (ULONG_PTR)fast_GetCurrentThreadId,
        (ULONG_PTR)fast_GetCurrentProcessId,
        (ULONG_PTR)fast_GetLastError,
        (ULONG_PTR)fast_SetLastError,
        (ULONG_PTR)fast_GetProcessHeap,
        (ULONG_PTR)fast_NtCurrentTeb,
        (ULONG_PTR)fast_GetCurrentProcess,
        (ULONG_PTR)fast_GetCurrentThread,
        (ULONG_PTR)fast_GetTickCount,
        (ULONG_PTR)fast_GetTickCount64,
        (ULONG_PTR)fast_GetSystemTimeAsFileTime,
        (ULONG_PTR)fast_GetSystemTimePreciseAsFileTime,
        (ULONG_PTR)fast_QueryPerformanceCounter,
        (ULONG_PTR)fast_QueryPerformanceFrequency,
        (ULONG_PTR)fast_TlsGetValue,
        (ULONG_PTR)fast_TlsSetValue,
        (ULONG_PTR)fast_EnterCriticalSection,
        (ULONG_PTR)fast_LeaveCriticalSection,
        (ULONG_PTR)fast_RtlRunOnceExecuteOnce,
        (ULONG_PTR)fast_IsProcessorFeaturePresent,
        (ULONG_PTR)fast_RtlInitUnicodeString,
        (ULONG_PTR)fast_GetVersion,
        (ULONG_PTR)fast_GetActiveProcessorCount,
        (ULONG_PTR)fast_RtlEqualUnicodeString,
        (ULONG_PTR)fast_RtlAcquireSRWLockExclusive,
        (ULONG_PTR)fast_RtlTryAcquireSRWLockExclusive,
        (ULONG_PTR)fast_RtlReleaseSRWLockExclusive,
        (ULONG_PTR)fast_RtlAcquireSRWLockShared,
        (ULONG_PTR)fast_RtlTryAcquireSRWLockShared,
        (ULONG_PTR)fast_RtlReleaseSRWLockShared,
        (ULONG_PTR)fast_EncodePointer,
        (ULONG_PTR)fast_DecodePointer,
    };
    static ULONG_PTR min_tramp = 0, max_tramp = 0;
    SIZE_T i;

    if (!min_tramp)
    {
        ULONG_PTR lo = tramps[0], hi = tramps[0];
        for (i = 1; i < ARRAY_SIZE(tramps); i++)
        {
            if (tramps[i] < lo) lo = tramps[i];
            if (tramps[i] > hi) hi = tramps[i];
        }
        min_tramp = lo;
        max_tramp = hi;
    }

    if (addr < min_tramp || addr > max_tramp) return FALSE;

    for (i = 0; i < ARRAY_SIZE(tramps); i++)
        if (tramps[i] == addr) return TRUE;
    return FALSE;
}

struct iat_range
{
    ULONG_PTR start;
    ULONG_PTR end;
};

static BOOL is_in_iat_ranges( ULONG_PTR slot, const struct iat_range *ranges, unsigned int count )
{
    unsigned int i;
    if (slot & 7) return FALSE;
    for (i = 0; i < count; i++)
        if (slot >= ranges[i].start && slot < ranges[i].end)
            return TRUE;
    return FALSE;
}

void patch_module_call_sites( void *module_base, SIZE_T image_size )
{
    const IMAGE_NT_HEADERS *nt;
    const IMAGE_IMPORT_DESCRIPTOR *imports;
    const IMAGE_SECTION_HEADER *sec;
    struct iat_range ranges[128];
    unsigned int num_ranges = 0;
    DWORD imp_size = 0;
    WORD i;

    if (!is_fast_trampoline_enabled() || !module_base || !image_size) return;

    nt = RtlImageNtHeader( module_base );
    if (!nt || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return;

    imports = RtlImageDirectoryEntryToData( module_base, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &imp_size );
    if (!imports) return;

    while (imports[num_ranges].Name && imports[num_ranges].FirstThunk && num_ranges < ARRAY_SIZE(ranges))
    {
        const IMAGE_THUNK_DATA *thunk = (const IMAGE_THUNK_DATA *)((const BYTE *)module_base + imports[num_ranges].FirstThunk);
        ULONG_PTR start = (ULONG_PTR)thunk;
        while ((ULONG_PTR)thunk - (ULONG_PTR)module_base + sizeof(*thunk) <= image_size && thunk->u1.Function)
            thunk++;
        ranges[num_ranges].start = start;
        ranges[num_ranges].end = (ULONG_PTR)thunk;
        num_ranges++;
    }
    if (!num_ranges) return;

    sec = IMAGE_FIRST_SECTION( nt );
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        BYTE *sec_start;
        SIZE_T sec_size, pos;
        void *prot_base = NULL;
        SIZE_T prot_size = 0;
        ULONG old_prot = 0;
        BOOL writable = FALSE;

        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        if (sec->VirtualAddress >= image_size) continue;

        sec_size = sec->Misc.VirtualSize;
        if (sec_size > image_size - sec->VirtualAddress)
            sec_size = image_size - sec->VirtualAddress;
        if (sec_size < 6) continue;

        sec_start = (BYTE *)module_base + sec->VirtualAddress;
        for (pos = 0; pos <= sec_size - 6; pos++)
        {
            INT32 disp;
            ULONG_PTR slot, target;
            INT64 rel;

            if (sec_start[pos] != 0xFF || sec_start[pos + 1] != 0x15) continue;

            disp = *(const INT32 *)(sec_start + pos + 2);
            slot = (ULONG_PTR)(sec_start + pos + 6) + (INT64)disp;
            if (!is_in_iat_ranges( slot, ranges, num_ranges )) continue;

            target = *(const ULONG_PTR *)slot;
            if (!is_fast_trampoline_addr( target )) continue;

            rel = (INT64)target - (INT64)(ULONG_PTR)(sec_start + pos + 6);
            if (rel < INT32_MIN || rel > INT32_MAX) continue;

            if (!writable)
            {
                prot_base = sec_start;
                prot_size = sec_size;
                if (NtProtectVirtualMemory( NtCurrentProcess(), &prot_base, &prot_size,
                                            PAGE_EXECUTE_READWRITE, &old_prot ))
                    break;
                writable = TRUE;
            }

            sec_start[pos] = 0x90;
            sec_start[pos + 1] = 0xE8;
            *(INT32 *)(sec_start + pos + 2) = (INT32)rel;
            pos += 5;
        }

        if (writable)
        {
            NtProtectVirtualMemory( NtCurrentProcess(), &prot_base, &prot_size, old_prot, &old_prot );
            NtFlushInstructionCache( NtCurrentProcess(), sec_start, sec_size );
        }
    }
}

#endif /* __x86_64__ */
