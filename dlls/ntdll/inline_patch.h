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

#ifndef __NTDLL_INLINE_PATCH_H
#define __NTDLL_INLINE_PATCH_H

#include "windef.h"
#include "winternl.h"

#ifdef __x86_64__

enum fast_trampoline_id {
    FAST_TRAMP_GET_CURRENT_THREAD_ID = 0,
    FAST_TRAMP_GET_CURRENT_PROCESS_ID,
    FAST_TRAMP_GET_CURRENT_PROCESS,
    FAST_TRAMP_GET_CURRENT_THREAD,
    FAST_TRAMP_GET_LAST_ERROR,
    FAST_TRAMP_SET_LAST_ERROR,
    FAST_TRAMP_GET_PROCESS_HEAP,
    FAST_TRAMP_GET_TEB,
    FAST_TRAMP_GET_TICK_COUNT,
    FAST_TRAMP_GET_SYSTEM_TIME_AS_FILE_TIME,
    FAST_TRAMP_TLS_GET_VALUE,
    FAST_TRAMP_TLS_SET_VALUE,
    FAST_TRAMP_GET_TICK_COUNT64,
    FAST_TRAMP_ENTER_CRITICAL_SECTION,
    FAST_TRAMP_LEAVE_CRITICAL_SECTION,
    FAST_TRAMP_RTL_RUN_ONCE_EXECUTE_ONCE,
    FAST_TRAMP_IS_PROCESSOR_FEATURE_PRESENT,
    FAST_TRAMP_RTL_INIT_UNICODE_STRING,
    FAST_TRAMP_QUERY_PERFORMANCE_FREQUENCY,
    FAST_TRAMP_GET_SYSTEM_TIME_PRECISE_AS_FILE_TIME,
    FAST_TRAMP_GET_VERSION,
    FAST_TRAMP_GET_ACTIVE_PROCESSOR_COUNT,
    FAST_TRAMP_RTL_EQUAL_UNICODE_STRING,
    FAST_TRAMP_COUNT
};

/* Native x86_64 inline implementation of IsProcessorFeaturePresent /
   RtlIsProcessorFeaturePresent: direct KUSER_SHARED_DATA->ProcessorFeatures
   byte load (0x7ffe0274) with a bounds check against PROCESSOR_FEATURE_MAX. */
extern BOOL WINAPI fast_IsProcessorFeaturePresent( DWORD feature );

/* Native x86_64 inline implementation of QueryPerformanceFrequency /
   RtlQueryPerformanceFrequency. Reads KUSER_SHARED_DATA->QpcFrequency
   (0x7ffe0300) directly under QpcBypassEnabled check with TICKSPERSEC fallback. */
extern BOOL WINAPI fast_QueryPerformanceFrequency( LARGE_INTEGER *frequency );

/* Native x86_64 inline implementation of GetSystemTimePreciseAsFileTime: seqlock
   read of KUSER_SHARED_DATA->SystemTime (0x7ffe0014). */
extern void WINAPI fast_GetSystemTimePreciseAsFileTime( void *time );

/* Native x86_64 inline implementation of RtlInitUnicodeString: a single
   unaligned 16-byte SSE2 probe locates the terminator for strings of up to 7
   wide characters (Buffer aliases the source, Length is the byte length,
   MaximumLength adds the terminator). Page-crossing probes and longer strings
   tail-call the generic RtlInitUnicodeString, so the trampoline is
   semantically identical to the export it replaces. */
extern void WINAPI fast_RtlInitUnicodeString( PUNICODE_STRING target, PCWSTR source );

/* Native x86_64 inline implementation of GetVersion: builds the packed Win32
   version word straight out of KUSER_SHARED_DATA - NtMajorVersion (0x7ffe026c),
   NtMinorVersion (0x7ffe0270) and NtBuildNumber (0x7ffe0260) - the values wineboot
   publishes once into the shared user page. Returns
   (Major & 0xff) | ((Minor & 0xff) << 8) | ((Build & 0x7fff) << 16), replacing
   kernelbase's GetVersionExW + RtlGetVersion round trip. */
extern DWORD WINAPI fast_GetVersion( void );

/* Native x86_64 inline implementation of GetActiveProcessorCount: reads
   KUSER_SHARED_DATA->ActiveProcessorCount (0x7ffe03c0, include/ddk/wdm.h; filled
   per process in dlls/ntdll/unix/virtual.c init_shared_data) instead of running
   the GetLogicalProcessorInformation allocation loop. Group 0 and
   ALL_PROCESSOR_GROUPS (0xffff) both yield the single group's count, any other
   group returns 0. The WORD argument is zero-extended because the Win64 ABI
   leaves the upper 16 bits of %ecx undefined. */
extern DWORD WINAPI fast_GetActiveProcessorCount( WORD group );

/* Native x86_64 inline implementation of RtlEqualUnicodeString: SSE2 fast path for
   case-sensitive equality of strings up to 16 bytes, using exact-size 2/4/8/16 byte
   loads (a 16-byte movdqu only runs when Length == 16, so MaximumLength keeps every
   read inside mapped memory). Pointer identity, zero length, identical length and
   identical buffers short-circuit; the case-insensitive path, lengths that are not
   an exact load width, and lengths above 16 tail-jump to the C implementation. */
extern BOOLEAN WINAPI fast_RtlEqualUnicodeString( const UNICODE_STRING *s1, const UNICODE_STRING *s2,
                                                  BOOLEAN case_in_sensitive );

/* Fast IAT trampoline lookup */
ULONG_PTR get_fast_iat_trampoline( const char *func_name );

/* Heuristic evaluation: should module participate in inline hotpatching? */
BOOL is_module_eligible_for_inline_patch( const WCHAR *module_name, void *base );

/* Scan and patch known calling patterns (TlsGetValue, GetCurrentThreadId) in code section */
void patch_module_call_sites( void *module_base, SIZE_T image_size );

#endif /* __x86_64__ */

#endif /* __NTDLL_INLINE_PATCH_H */
