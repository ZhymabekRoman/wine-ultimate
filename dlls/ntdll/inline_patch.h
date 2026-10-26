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
    FAST_TRAMP_GET_LAST_ERROR,
    FAST_TRAMP_SET_LAST_ERROR,
    FAST_TRAMP_GET_PROCESS_HEAP,
    FAST_TRAMP_GET_TEB,
    FAST_TRAMP_COUNT
};

/* Fast IAT trampoline lookup */
ULONG_PTR get_fast_iat_trampoline( const char *func_name );

/* Heuristic evaluation: should module participate in inline hotpatching? */
BOOL is_module_eligible_for_inline_patch( const WCHAR *module_name, void *base );

/* Scan and patch known calling patterns (TlsGetValue, GetCurrentThreadId) in code section */
void patch_module_call_sites( void *module_base, SIZE_T image_size );

#endif /* __x86_64__ */

#endif /* __NTDLL_INLINE_PATCH_H */
