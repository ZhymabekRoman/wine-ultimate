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
    FAST_TRAMP_COUNT
};

/* Native x86_64 inline implementation of IsProcessorFeaturePresent /
   RtlIsProcessorFeaturePresent: direct KUSER_SHARED_DATA->ProcessorFeatures
   byte load (0x7ffe0274) with a bounds check against PROCESSOR_FEATURE_MAX. */
extern BOOL WINAPI fast_IsProcessorFeaturePresent( DWORD feature );

/* Native x86_64 inline implementation of RtlInitUnicodeString: a single
   unaligned 16-byte SSE2 probe locates the terminator for strings of up to 7
   wide characters (Buffer aliases the source, Length is the byte length,
   MaximumLength adds the terminator). Page-crossing probes and longer strings
   tail-call the generic RtlInitUnicodeString, so the trampoline is
   semantically identical to the export it replaces. */
extern void WINAPI fast_RtlInitUnicodeString( PUNICODE_STRING target, PCWSTR source );

/* Fast IAT trampoline lookup */
ULONG_PTR get_fast_iat_trampoline( const char *func_name );

/* Heuristic evaluation: should module participate in inline hotpatching? */
BOOL is_module_eligible_for_inline_patch( const WCHAR *module_name, void *base );

/* Scan and patch known calling patterns (TlsGetValue, GetCurrentThreadId) in code section */
void patch_module_call_sites( void *module_base, SIZE_T image_size );

#endif /* __x86_64__ */

#endif /* __NTDLL_INLINE_PATCH_H */
