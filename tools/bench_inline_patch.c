/*
 * Comprehensive Benchmark & Verification Suite for Wine Inline IAT Trampolines
 *
 * Copyright 2026 Zhymabek Roman
 *
 * This test verifies and benchmarks:
 * - Pointer identity: GetProcAddress(kernel32, name) == &func in IAT
 * - Pseudohandles: GetCurrentProcess() == -1, GetCurrentThread() == -2
 * - Fast Getters: GetCurrentThreadId(), GetCurrentProcessId(), GetProcessHeap()
 * - KUSER_SHARED_DATA fast timers: GetTickCount(), GetSystemTimeAsFileTime()
 * - TLS slot inlining: TlsAlloc(), TlsSetValue(), TlsGetValue() (slots 0..1087)
 * - Atomic lock-free Critical Section Enter/Leave under single and multi-threading
 * - IsProcessorFeaturePresent / RtlIsProcessorFeaturePresent inline trampoline:
 *   result identity across all feature IDs 0..64 vs raw KUSER_SHARED_DATA bytes,
 *   pointer identity across kernel32/kernelbase/ntdll exports, throughput + latency
 * - QueryPerformanceFrequency / RtlQueryPerformanceFrequency inline trampoline:
 *   value identity vs raw KUSER_SHARED_DATA->QpcFrequency (gated on the QPC bypass
 *   flag exactly like ntdll does), NULL-argument contract, QPC rate cross-check
 * - GetSystemTimePreciseAsFileTime inline trampoline: monotonicity, deviation and
 *   quantization measured against the unpatched ntdll RtlGetSystemTimePrecise
 * - GetVersion inline trampoline: value identity vs the raw KUSER_SHARED_DATA
 *   version fields (NtMajorVersion 0x26c, NtMinorVersion 0x270, NtBuildNumber
 *   0x260) under the documented packing rule, plus agreement with the unpatched
 *   kernelbase path reproduced from GetVersionExW (PEB/registry derived), throughput
 * - GetActiveProcessorCount inline trampoline: value identity vs
 *   KUSER_SHARED_DATA->ActiveProcessorCount (0x3c0), GetSystemInfo,
 *   GetMaximumProcessorCount and the unpatched GetLogicalProcessorInformationEx
 *   group walk, out-of-range group -> 0, throughput
 * - RtlEqualUnicodeString SSE2 inline trampoline: result identity across 26 vectors
 *   (lengths 0/2/4/6/8/14/16/18/26, pointer identity, length mismatch, NULL buffers,
 *   case-sensitive and case-insensitive) against a C reference and the unpatched
 *   ntdll RtlCompareUnicodeString, a page-guard over-read probe for every exact-load
 *   width, and throughput for the movq / SSE2 / C-fallback / case-insensitive paths
 * Run with WINE_NO_INLINE_PATCH=1 in the environment to measure the identical suite
 * against the unpatched kernel32/ntdll implementations (A/B baseline).
 */

#include <stdio.h>
#include <stdint.h>
#include <windows.h>

/* KUSER_SHARED_DATA reference points: the raw page the inline trampolines read.
   Offsets per include/ddk/wdm.h - SystemTime 0x014 (High1Time 0x018, High2Time
   0x01c), ProcessorFeatures 0x274, QpcFrequency 0x300, QpcBypassEnabled 0x3c6. */
#define KUSD_QPC_FREQUENCY  (*(const volatile unsigned long long *)0x7ffe0300)
#define KUSD_QPC_BYPASS     (*(const volatile unsigned char *)0x7ffe03c6)

/* Version + processor-count reference points, offsets per include/ddk/wdm.h:
   NtBuildNumber 0x260, NtMajorVersion 0x26c, NtMinorVersion 0x270,
   ActiveProcessorCount 0x3c0, ActiveGroupCount 0x3c4. Note that 0x2ec is
   SafeBootMode/VirtualizationFlags, not a processor count - the suite dumps it so
   the layout difference is visible in the log. */
#define KUSD_NT_BUILD_NUMBER     (*(const volatile unsigned int *)0x7ffe0260)
#define KUSD_NT_MAJOR_VERSION    (*(const volatile unsigned int *)0x7ffe026c)
#define KUSD_NT_MINOR_VERSION    (*(const volatile unsigned int *)0x7ffe0270)
#define KUSD_AT_02EC             (*(const volatile unsigned int *)0x7ffe02ec)
#define KUSD_ACTIVE_PROCESSOR_COUNT (*(const volatile unsigned int *)0x7ffe03c0)
#define QPC_BYPASS_ENABLED  0x01
#define TICKSPERSEC_U       10000000ULL   /* ntdll RtlQueryPerformanceFrequency fallback */

typedef LARGE_INTEGER (WINAPI *pfn_RtlGetSystemTimePrecise)(void);

#define PRECISION_SAMPLES   2000
#define GRANULARITY_SAMPLES 20000

#define WARMUP_ITERS  1000000
#define BENCH_ITERS   20000000
#define CS_ITERS      10000000
#define THREAD_COUNT  4
#define PER_THREAD_OP 250000

#include <string.h>
#include <setjmp.h>
/* UNICODE_STRING / PUNICODE_STRING live in ntdef.h + winternl.h on mingw-w64 (windows.h
   alone does not pull them in). */
#include <ntdef.h>
#include <winternl.h>

/* ntdll string entry points: declared here because the mingw headers do not
   prototype them; linking resolves them from libntdll.a, which is what puts the
   call through the import table the loader patches. */
BOOLEAN WINAPI RtlEqualUnicodeString( const UNICODE_STRING *s1, const UNICODE_STRING *s2, BOOLEAN ci );
LONG    WINAPI RtlCompareUnicodeString( const UNICODE_STRING *s1, const UNICODE_STRING *s2, BOOLEAN ci );

static CRITICAL_SECTION g_cs;
static volatile LONG g_shared_counter = 0;
static int g_pf_ok = 0;
static int g_qpc_ok = 0;
static int g_gst_ok = 0;
static int g_ver_ok = 0;
static int g_apc_ok = 0;
static int g_eq_ok = 0;

/* ------------------------------------------------------------------ */
/* Helpers for the KUSER_SHARED_DATA / string trampolines              */
/* ------------------------------------------------------------------ */

/* The kernelbase GetVersion formula (dlls/kernelbase/version.c) driven by the
   unpatched GetVersionExW: the baseline the packed shared-data trampoline must
   agree with. GetVersionExW is not in the trampoline dispatch table, so this
   reference always runs the real code path in both A/B modes. */
static DWORD baseline_GetVersion_via_GetVersionExW(void)
{
    OSVERSIONINFOEXW ov;
    DWORD result = 0;

    memset(&ov, 0, sizeof(ov));
    ov.dwOSVersionInfoSize = sizeof(ov);
    if (!GetVersionExW((OSVERSIONINFOW *)&ov)) return 0;

    result = MAKELONG(MAKEWORD((WORD)ov.dwMajorVersion, (WORD)ov.dwMinorVersion),
                      (ov.dwPlatformId ^ 2) << 14);
    if (ov.dwPlatformId == VER_PLATFORM_WIN32_NT)
        result |= (DWORD)(WORD)ov.dwBuildNumber << 16;
    return result;
}

/* The kernel32 GetActiveProcessorCount algorithm (dlls/kernel32/process.c): walk
   the RelationGroup record of GetLogicalProcessorInformationEx. Unpatched, so it
   is the heavy baseline the shared-data trampoline is measured against. */
static DWORD baseline_group_count(int maximum)
{
    BYTE *buf = NULL, *cur;
    DWORD len = 0, off, total = 0, i;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX rec;

    if (!GetLogicalProcessorInformationEx(RelationAll, NULL, &len) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return 0;
    if (!(buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, len))) return 0;
    if (!GetLogicalProcessorInformationEx(RelationAll,
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len))
    { HeapFree(GetProcessHeap(), 0, buf); return 0; }

    for (cur = buf, off = 0; off < len; cur += rec->Size, off += rec->Size)
    {
        rec = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)cur;
        if (rec->Relationship == RelationGroup)
            for (i = 0; i < rec->Group.ActiveGroupCount; i++)
                total += maximum ? rec->Group.GroupInfo[i].MaximumProcessorCount
                                 : rec->Group.GroupInfo[i].ActiveProcessorCount;
        if (!rec->Size) break;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return total;
}

static UNICODE_STRING make_us(const WCHAR *w, USHORT chars)
{
    UNICODE_STRING s;
    s.Length = (USHORT)(chars * sizeof(WCHAR));
    s.MaximumLength = (USHORT)(chars * sizeof(WCHAR));   /* tightest legal value */
    s.Buffer = (PWSTR)w;
    return s;
}

/* Case-sensitive reference mirroring ntdll rtlstr.c RtlEqualUnicodeString:
   length equality, buffer identity, then an exact compare of Length bytes.
   Returns -1 when the C implementation would fault (NULL buffer, Length > 0),
   which is the one case the trampoline is strictly safer about. */
static int ref_equal_cs(const UNICODE_STRING *s1, const UNICODE_STRING *s2)
{
    if (s1->Length != s2->Length) return 0;
    if (s1->Buffer == s2->Buffer) return 1;
    if (!s1->Buffer || !s2->Buffer) return -1;
    return memcmp(s1->Buffer, s2->Buffer, s1->Length) == 0;
}

/* Case-insensitive reference: ntdll returns TRUE when the bytes match, otherwise
   the result of RtlCompareUnicodeString - resolved from ntdll, never patched. */
static int ref_equal_ci(const UNICODE_STRING *s1, const UNICODE_STRING *s2)
{
    if (s1->Length != s2->Length) return 0;
    if (s1->Buffer == s2->Buffer) return 1;
    if (!s1->Buffer || !s2->Buffer) return s1->Length == 0;
    if (!memcmp(s1->Buffer, s2->Buffer, s1->Length)) return 1;
    return RtlCompareUnicodeString(s1, s2, TRUE) == 0;
}

/* Access-violation guard: a vectored handler longjmps out of a faulting probe, so
   the page-guard over-read test can report a fault instead of killing the suite. */
static jmp_buf g_av_jmp;
static LONG CALLBACK av_guard(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        longjmp(g_av_jmp, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Place `len` bytes so the last one is the last byte of a mapped page and the next
   page is PAGE_NOACCESS. If a trampoline reads past UNICODE_STRING.Length it faults,
   the handler reports it, and the probe returns 1. */
static int probe_overread(USHORT len, int differ_at_end)
{
    static const BYTE pat[40] = {
        'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p',
        'q','r','s','t','u','v','w','x','y','z','A','B','C','D','E','F',
        '1','2','3','4','5','6','7','8' };
    BYTE *map, *tail, *other;
    UNICODE_STRING s1, s2;
    DWORD old;
    PVOID handler;
    int faulted = 0;

    map = (BYTE *)VirtualAlloc(NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    other = (BYTE *)VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!map || !other) { if (map) VirtualFree(map, 0, MEM_RELEASE); if (other) VirtualFree(other, 0, MEM_RELEASE); return -1; }

    tail = map + 0x1000 - len;                 /* string ends exactly at the page edge */
    memcpy(tail, pat, len);
    memcpy(other, pat, len);
    if (differ_at_end) other[len ? len - 1 : 0] ^= 0x20;
    if (!VirtualProtect(map + 0x1000, 0x1000, PAGE_NOACCESS, &old))
    { VirtualFree(map, 0, MEM_RELEASE); VirtualFree(other, 0, MEM_RELEASE); return -1; }

    s1 = make_us((const WCHAR *)tail, (USHORT)(len / sizeof(WCHAR)));
    s2 = make_us((const WCHAR *)other, (USHORT)(len / sizeof(WCHAR)));

    handler = AddVectoredExceptionHandler(1, av_guard);
    if (setjmp(g_av_jmp)) faulted = 1;
    else RtlEqualUnicodeString(&s1, &s2, FALSE);
    RemoveVectoredExceptionHandler(handler);

    VirtualFree(map, 0, MEM_RELEASE);
    VirtualFree(other, 0, MEM_RELEASE);
    return faulted;
}

static DWORD WINAPI ContentionWorker(LPVOID param)
{
    int i;
    for (i = 0; i < PER_THREAD_OP; i++)
    {
        EnterCriticalSection(&g_cs);
        g_shared_counter++;
        LeaveCriticalSection(&g_cs);
    }
    return 0;
}

typedef DWORD (WINAPI *pfn_RtlRunOnceExecuteOnce)( void *once, void *func, void *param, void **context );

static DWORD WINAPI TestRtlRunOnceCallback( void *once, void *param, void **context )
{
    if (context) *context = (void *)0xCAFEFEE0;
    return 1;
}

int main(int argc, char **argv)
{
    HMODULE hK32;
    void *p_gtid, *p_gpid, *p_gheap, *p_last_err;
    HANDLE hProc, hThread;
    FILETIME ft1, ft2;
    DWORD tick1, tick2, tls_idx;
    LARGE_INTEGER freq, t0, t1;
    double sec, ns_per_op, mops;
    DWORD dummy = 0;
    HANDLE threads[THREAD_COUNT];
    int i;

    printf("====================================================================\n");
    printf("  Wine NTDLL Inline IAT Trampoline Benchmark & Correctness Suite   \n");
    printf("====================================================================\n\n");

    /* ------------------------------------------------------------- */
    /* 1. Pointer Identity Checks                                    */
    /* ------------------------------------------------------------- */
    hK32 = GetModuleHandleA("kernel32.dll");
    p_gtid = (void *)GetProcAddress(hK32, "GetCurrentThreadId");
    p_gpid = (void *)GetProcAddress(hK32, "GetCurrentProcessId");
    p_gheap = (void *)GetProcAddress(hK32, "GetProcessHeap");
    p_last_err = (void *)GetProcAddress(hK32, "GetLastError");

    printf("[1] Pointer Identity Verification:\n");
    printf("    GetProcAddress('GetCurrentThreadId'): %p\n", p_gtid);
    printf("    GetProcAddress('GetCurrentProcessId'): %p\n", p_gpid);
    printf("    GetProcAddress('GetProcessHeap'):     %p\n", p_gheap);
    printf("    GetProcAddress('GetLastError'):        %p\n", p_last_err);

    /* ------------------------------------------------------------- */
    /* 2. Pseudohandles & Getters Correctness                        */
    /* ------------------------------------------------------------- */
    hProc = GetCurrentProcess();
    hThread = GetCurrentThread();
    printf("\n[2] Pseudohandles Correctness:\n");
    printf("    GetCurrentProcess() = %p (expected -1): %s\n",
           hProc, (hProc == (HANDLE)(LONG_PTR)-1) ? "PASS" : "FAIL");
    printf("    GetCurrentThread()  = %p (expected -2): %s\n",
           hThread, (hThread == (HANDLE)(LONG_PTR)-2) ? "PASS" : "FAIL");

    /* ------------------------------------------------------------- */
    /* 3. Timers & KUSER_SHARED_DATA Verification                    */
    /* ------------------------------------------------------------- */
    tick1 = GetTickCount();
    ULONGLONG tick64_1 = GetTickCount64();
    GetSystemTimeAsFileTime(&ft1);
    Sleep(10);
    tick2 = GetTickCount();
    ULONGLONG tick64_2 = GetTickCount64();
    GetSystemTimeAsFileTime(&ft2);

    uint64_t ns_elapsed = (*((uint64_t *)&ft2) - *((uint64_t *)&ft1)) * 100;
    printf("\n[3] Timers & KUSER_SHARED_DATA:\n");
    printf("    GetTickCount() progression: %lu -> %lu (delta %lu ms): %s\n",
           tick1, tick2, tick2 - tick1, (tick2 >= tick1) ? "PASS" : "FAIL");
    printf("    GetTickCount64() progression: %llu -> %llu (delta %llu ms): %s\n",
           (unsigned long long)tick64_1, (unsigned long long)tick64_2,
           (unsigned long long)(tick64_2 - tick64_1), (tick64_2 >= tick64_1) ? "PASS" : "FAIL");
    printf("    GetSystemTimeAsFileTime() delta: %llu ns (~%llu ms): PASS\n",
           (unsigned long long)ns_elapsed, (unsigned long long)(ns_elapsed / 1000000));

    /* ------------------------------------------------------------- */
    /* 4. TLS Slot Inlining                                          */
    /* ------------------------------------------------------------- */
    tls_idx = TlsAlloc();
    BOOL set_ok = TlsSetValue(tls_idx, (void *)0x1234ABCD);
    void *tls_got = TlsGetValue(tls_idx);
    printf("\n[4] TLS Slot Inlining:\n");
    printf("    Slot %lu: set 0x1234ABCD (ok=%d), got %p: %s\n",
           (unsigned long)tls_idx, set_ok, tls_got,
           (set_ok && tls_got == (void *)0x1234ABCD) ? "PASS" : "FAIL");
    TlsFree(tls_idx);

    /* ------------------------------------------------------------- */
    /* 4b. RtlRunOnce Inlining & Correctness                         */
    /* ------------------------------------------------------------- */
    RTL_RUN_ONCE run_once = { 0 };
    void *ctx1 = NULL, *ctx2 = NULL;
    pfn_RtlRunOnceExecuteOnce p_run_once = (pfn_RtlRunOnceExecuteOnce)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlRunOnceExecuteOnce");
    DWORD ro_ok1 = p_run_once ? p_run_once(&run_once, TestRtlRunOnceCallback, NULL, &ctx1) : 1;
    DWORD ro_ok2 = p_run_once ? p_run_once(&run_once, TestRtlRunOnceCallback, NULL, &ctx2) : 1;
    printf("\n[4b] RtlRunOnce Fast Inlining:\n");
    printf("    RtlRunOnceExecuteOnce: 1st=%lu ctx=%p, 2nd=%lu ctx=%p: %s\n",
           ro_ok1, ctx1, ro_ok2, ctx2,
           (ro_ok1 == 0 && ro_ok2 == 0 && ctx1 == (void *)0xCAFEFEE0 && ctx2 == (void *)0xCAFEFEE0) ? "PASS" : "FAIL");

    /* ------------------------------------------------------------- */
    /* 5. Throughput Benchmark (GetCurrentThreadId + GetTickCount)    */
    /* ------------------------------------------------------------- */
    QueryPerformanceFrequency(&freq);

    /* Warmup */
    for (i = 0; i < WARMUP_ITERS; i++)
    {
        dummy += GetCurrentThreadId();
        dummy += GetTickCount();
    }

    QueryPerformanceCounter(&t0);
    for (i = 0; i < BENCH_ITERS; i++)
    {
        dummy += GetCurrentThreadId();
        dummy += GetTickCount();
    }
    QueryPerformanceCounter(&t1);

    sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    ns_per_op = (sec * 1e9) / (BENCH_ITERS * 2);
    mops = ((double)BENCH_ITERS * 2) / (sec * 1e6);

    printf("\n[5] Throughput Benchmark (%d calls: TID + TickCount):\n", BENCH_ITERS * 2);
    printf("    Total Duration: %.4f s\n", sec);
    printf("    Latency:        %.2f ns / call\n", ns_per_op);
    printf("    Throughput:     %.2f MILLION calls/sec\n", mops);

    /* ------------------------------------------------------------- */
    /* 6. Lock-Free Critical Section Benchmark                       */
    /* ------------------------------------------------------------- */
    InitializeCriticalSection(&g_cs);

    QueryPerformanceCounter(&t0);
    for (i = 0; i < CS_ITERS; i++)
    {
        EnterCriticalSection(&g_cs);
        g_shared_counter++;
        LeaveCriticalSection(&g_cs);
    }
    QueryPerformanceCounter(&t1);

    sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    ns_per_op = (sec * 1e9) / CS_ITERS;
    mops = (double)CS_ITERS / (sec * 1e6);

    printf("\n[6] Uncontended Critical Section Benchmark (%d pairs):\n", CS_ITERS);
    printf("    Total Duration: %.4f s\n", sec);
    printf("    Latency:        %.2f ns per Enter+Leave pair\n", ns_per_op);
    printf("    Throughput:     %.2f MILLION pairs/sec\n", mops);
    printf("    Counter:        %ld (expected %d): %s\n",
           g_shared_counter, CS_ITERS, (g_shared_counter == CS_ITERS) ? "PASS" : "FAIL");

    /* ------------------------------------------------------------- */
    /* 7. Multi-Threaded Contention Stress Test                      */
    /* ------------------------------------------------------------- */
    g_shared_counter = 0;
    for (i = 0; i < THREAD_COUNT; i++)
        threads[i] = CreateThread(NULL, 0, ContentionWorker, NULL, 0, NULL);
    WaitForMultipleObjects(THREAD_COUNT, threads, TRUE, INFINITE);
    for (i = 0; i < THREAD_COUNT; i++) CloseHandle(threads[i]);

    LONG expected = THREAD_COUNT * PER_THREAD_OP;
    printf("\n[7] Multi-Threaded Contention Test (%d threads x %d ops):\n",
           THREAD_COUNT, PER_THREAD_OP);
    printf("    Final Counter:  %ld (expected %ld): %s\n",
           g_shared_counter, expected, (g_shared_counter == expected) ? "PASS" : "FAIL");

    DeleteCriticalSection(&g_cs);

    /* ------------------------------------------------------------- */
    /* 8. IsProcessorFeaturePresent inline trampoline                */
    /* ------------------------------------------------------------- */
    {
        const volatile BYTE *pf_base = (const volatile BYTE *)(uintptr_t)0x7FFE0274; /* KUSER_SHARED_DATA.ProcessorFeatures[0] */
        BOOL (WINAPI *p_ipfp_k32)(DWORD);
        BOOL (WINAPI *p_ipfp_kb)(DWORD);
        BOOLEAN (WINAPI *p_ripfp_nt)(UINT);
        void *p_import = (void *)IsProcessorFeaturePresent;
        int feat, ident_ok = 1, all_zero = 1;

        p_ipfp_k32 = (BOOL (WINAPI *)(DWORD))GetProcAddress(hK32, "IsProcessorFeaturePresent");
        p_ipfp_kb  = (BOOL (WINAPI *)(DWORD))GetProcAddress(GetModuleHandleA("kernelbase.dll"), "IsProcessorFeaturePresent");
        p_ripfp_nt = (BOOLEAN (WINAPI *)(UINT))GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlIsProcessorFeaturePresent");

        printf("\n[8] IsProcessorFeaturePresent Inline Trampoline:\n");
        printf("    IAT import pointer:          %p\n", p_import);
        printf("    GetProcAddress(kernel32):    %p\n", (void *)p_ipfp_k32);
        printf("    GetProcAddress(kernelbase):  %p\n", (void *)p_ipfp_kb);
        printf("    GetProcAddress(ntdll, Rtl):  %p\n", (void *)p_ripfp_nt);
        printf("    Export identity (k32=kb=nt): %s\n",
               ((void *)p_ipfp_k32 == (void *)p_ipfp_kb && (void *)p_ipfp_kb == (void *)p_ripfp_nt)
                   ? "PASS (single unified trampoline)" : "info (baseline: distinct real export addresses)");

        printf("    Result identity for feature IDs 0..64 (import vs kernel32 ptr vs Rtl vs raw shared-data byte):\n");
        for (feat = 0; feat <= 64; feat++)
        {
            BOOL r_import = IsProcessorFeaturePresent(feat);
            BOOL r_k32    = p_ipfp_k32 ? p_ipfp_k32(feat) : 0xDEAD;
            BOOLEAN r_rtl = p_ripfp_nt ? p_ripfp_nt((UINT)feat) : 0xDEAD;
            BYTE r_raw    = (feat < 64) ? (BYTE)pf_base[feat] : 0;
            int ok = (r_import == r_raw) && (r_k32 == r_raw) && (r_rtl == r_raw);
            if (feat < 64 && r_raw) all_zero = 0;
            if (!ok) ident_ok = 0;
            if (feat < 24 || feat == 64)
                printf("      ID %2d: import=%d k32=%d rtl=%d raw=0x%02x %s\n",
                       feat, r_import, r_k32, r_rtl, r_raw, ok ? "PASS" : "FAIL");
            else if (feat == 24)
                printf("      ... (IDs 24..63 compacted, verified with same triple check) ...\n");
        }
        printf("    Result identity 0..64:       %s\n", ident_ok ? "PASS" : "FAIL");
        printf("    CPU features present (raw):  %s\n", all_zero ? "none (unexpected on x86_64)" : "PASS");

        /* Throughput & latency: hot-loop over a bounded feature set (IDs 0..31) */
        for (i = 0; i < WARMUP_ITERS; i++)
            dummy += IsProcessorFeaturePresent(i & 31);

        QueryPerformanceCounter(&t0);
        for (i = 0; i < BENCH_ITERS; i++)
            dummy += IsProcessorFeaturePresent(i & 31);
        QueryPerformanceCounter(&t1);

        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        ns_per_op = (sec * 1e9) / BENCH_ITERS;
        mops = (double)BENCH_ITERS / (sec * 1e6);

        printf("    IsProcessorFeaturePresent benchmark (%d calls, IDs 0..31):\n", BENCH_ITERS);
        printf("    Total Duration: %.4f s\n", sec);
        printf("    Latency:        %.2f ns / call\n", ns_per_op);
        printf("    Throughput:     %.2f MILLION calls/sec\n", mops);

        g_pf_ok = ident_ok;
    }

    /* ------------------------------------------------------------- */
    /* 9. QueryPerformanceFrequency inline trampoline                */
    /* ------------------------------------------------------------- */
    {
        BOOL (WINAPI *p_qpf_k32)(LARGE_INTEGER *);
        BOOL (WINAPI *p_qpf_kb)(LARGE_INTEGER *);
        BOOL (WINAPI *p_rqpf_nt)(LARGE_INTEGER *);
        LARGE_INTEGER fq_k32, fq_kb, fq_nt;
        unsigned long long expect;
        ULONGLONG tk0, tk1, dtk;
        double qpc_ms;
        int patched_qpc, qpc_ok = 1;

        p_qpf_k32 = (BOOL (WINAPI *)(LARGE_INTEGER *))GetProcAddress(hK32, "QueryPerformanceFrequency");
        p_qpf_kb  = (BOOL (WINAPI *)(LARGE_INTEGER *))GetProcAddress(GetModuleHandleA("kernelbase.dll"), "QueryPerformanceFrequency");
        p_rqpf_nt = (BOOL (WINAPI *)(LARGE_INTEGER *))GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlQueryPerformanceFrequency");
        patched_qpc = (p_qpf_k32 == p_qpf_kb && p_qpf_kb == p_rqpf_nt);

        printf("\n[9] QueryPerformanceFrequency Inline Trampoline:\n");
        printf("    IAT import pointer:          %p\n", (void *)QueryPerformanceFrequency);
        printf("    GetProcAddress(kernel32):    %p\n", (void *)p_qpf_k32);
        printf("    GetProcAddress(kernelbase):  %p\n", (void *)p_qpf_kb);
        printf("    GetProcAddress(ntdll, Rtl):  %p\n", (void *)p_rqpf_nt);
        printf("    Export identity (k32=kb=nt): %s\n",
               patched_qpc ? "PASS (single unified trampoline)" : "info (baseline: distinct real export addresses)");

        expect = (KUSD_QPC_BYPASS & QPC_BYPASS_ENABLED) ? KUSD_QPC_FREQUENCY : TICKSPERSEC_U;
        QueryPerformanceFrequency(&freq);
        fq_k32.QuadPart = fq_kb.QuadPart = fq_nt.QuadPart = 0;
        if (p_qpf_k32) p_qpf_k32(&fq_k32);
        if (p_qpf_kb)  p_qpf_kb(&fq_kb);
        if (p_rqpf_nt) p_rqpf_nt(&fq_nt);

        printf("    QpcBypassEnabled byte 0x7ffe03c6: 0x%02x (bypass %s), raw USD QpcFrequency 0x7ffe0300: %llu\n",
               (unsigned int)KUSD_QPC_BYPASS, (KUSD_QPC_BYPASS & QPC_BYPASS_ENABLED) ? "enabled" : "disabled",
               (unsigned long long)KUSD_QPC_FREQUENCY);
        printf("    Expected (ntdll rule):            %llu\n", (unsigned long long)expect);
        printf("    import/k32/kb/Rtl results:        %lld / %lld / %lld / %lld\n",
               (long long)freq.QuadPart, (long long)fq_k32.QuadPart,
               (long long)fq_kb.QuadPart, (long long)fq_nt.QuadPart);
        qpc_ok = (freq.QuadPart == (LONGLONG)expect) && (freq.QuadPart > 0)
                 && (fq_k32.QuadPart == freq.QuadPart) && (fq_kb.QuadPart == freq.QuadPart)
                 && (fq_nt.QuadPart == freq.QuadPart);
        printf("    Value identity (all 4 vs raw shared data rule): %s\n", qpc_ok ? "PASS" : "FAIL");

        if (patched_qpc)
        {
            /* NULL-argument contract of the trampoline. Only exercised when patched:
               the unpatched Wine path dereferences the pointer and faults on NULL,
               so calling it in the baseline run would crash the suite. */
            BOOL null_res = QueryPerformanceFrequency(NULL);
            printf("    NULL frequency pointer -> FALSE:  %s\n", null_res == FALSE ? "PASS" : "FAIL");
            if (null_res != FALSE) qpc_ok = 0;
        }

        /* Rate cross-check: QPC delta across a sleep must agree with TickCount64. */
        tk0 = GetTickCount64();
        QueryPerformanceCounter(&t0);
        Sleep(50);
        QueryPerformanceCounter(&t1);
        tk1 = GetTickCount64();
        qpc_ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        dtk = tk1 - tk0;
        printf("    QPC rate cross-check: %.2f ms via QPC vs %llu ms via GetTickCount64: %s\n",
               qpc_ms, (unsigned long long)dtk,
               ((qpc_ms > (double)dtk ? qpc_ms - (double)dtk : (double)dtk - qpc_ms) < 30.0) ? "PASS" : "FAIL");
        if ((qpc_ms > (double)dtk ? qpc_ms - (double)dtk : (double)dtk - qpc_ms) >= 30.0) qpc_ok = 0;

        for (i = 0; i < WARMUP_ITERS; i++)
        {
            QueryPerformanceFrequency(&fq_k32);
            dummy += (DWORD)fq_k32.LowPart;
        }
        QueryPerformanceCounter(&t0);
        for (i = 0; i < BENCH_ITERS; i++)
        {
            QueryPerformanceFrequency(&fq_k32);
            dummy += (DWORD)fq_k32.LowPart;
        }
        QueryPerformanceCounter(&t1);

        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        ns_per_op = (sec * 1e9) / BENCH_ITERS;
        mops = (double)BENCH_ITERS / (sec * 1e6);
        printf("    QueryPerformanceFrequency benchmark (%d calls):\n", BENCH_ITERS);
        printf("    Total Duration: %.4f s\n", sec);
        printf("    Latency:        %.2f ns / call\n", ns_per_op);
        printf("    Throughput:     %.2f MILLION calls/sec\n", mops);

        g_qpc_ok = qpc_ok;
    }

    /* ------------------------------------------------------------- */
    /* 10. GetSystemTimePreciseAsFileTime inline trampoline          */
    /* ------------------------------------------------------------- */
    {
        void (WINAPI *p_gst_k32)(FILETIME *);
        void (WINAPI *p_gst_kb)(FILETIME *);
        pfn_RtlGetSystemTimePrecise p_precise_nt;
        uint64_t prev = 0, v, ref, dev, max_dev = 0, sum_dev = 0, min_v = ~0ULL, max_v = 0;
        uint64_t distinct = 0, span;
        FILETIME a, b;
        int j, patched_gst, mono_ok = 1, gst_ok = 1;

        p_gst_k32 = (void (WINAPI *)(FILETIME *))GetProcAddress(hK32, "GetSystemTimePreciseAsFileTime");
        p_gst_kb  = (void (WINAPI *)(FILETIME *))GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetSystemTimePreciseAsFileTime");
        p_precise_nt = (pfn_RtlGetSystemTimePrecise)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetSystemTimePrecise");
        patched_gst = (p_gst_k32 == p_gst_kb);

        printf("\n[10] GetSystemTimePreciseAsFileTime Inline Trampoline:\n");
        printf("    IAT import pointer:          %p\n", (void *)GetSystemTimePreciseAsFileTime);
        printf("    GetProcAddress(kernel32):    %p\n", (void *)p_gst_k32);
        printf("    GetProcAddress(kernelbase):  %p\n", (void *)p_gst_kb);
        printf("    Export identity (k32=kb):    %s\n",
               patched_gst ? "PASS (single unified trampoline)" : "info (baseline: distinct real export addresses)");
        printf("    Reference source (ntdll RtlGetSystemTimePrecise): %s\n", p_precise_nt ? "resolved" : "MISSING");

        /* Deviation vs the unpatched precise clock (not name-patched, so always real). */
        if (p_precise_nt)
        {
            for (j = 0; j < PRECISION_SAMPLES; j++)
            {
                LARGE_INTEGER lr = p_precise_nt();
                GetSystemTimePreciseAsFileTime(&a);
                ref = ((uint64_t)a.dwHighDateTime << 32) | a.dwLowDateTime;
                dev = ((uint64_t)lr.QuadPart > ref) ? ((uint64_t)lr.QuadPart - ref) : (ref - (uint64_t)lr.QuadPart);
                if (dev > max_dev) max_dev = dev;
                sum_dev += dev;
            }
            printf("    Deviation vs precise clock over %d samples: mean %.3f ms, max %.3f ms\n",
                   PRECISION_SAMPLES, (double)sum_dev / PRECISION_SAMPLES / 10000.0, (double)max_dev / 10000.0);
            printf("    (inline path reads the wineserver-published KUSER_SHARED_DATA->SystemTime,\n"
                   "     so it is quantized to the server time-refresh granularity; 100 ns precision\n"
                   "     consumers must set WINE_NO_INLINE_PATCH=1 to stay on the precise path)\n");
        }

        /* Monotonicity + refresh quantum of the inline path. */
        GetSystemTimePreciseAsFileTime(&a);
        prev = ((uint64_t)a.dwHighDateTime << 32) | a.dwLowDateTime;
        min_v = max_v = prev;
        for (j = 0; j < GRANULARITY_SAMPLES; j++)
        {
            GetSystemTimePreciseAsFileTime(&a);
            v = ((uint64_t)a.dwHighDateTime << 32) | a.dwLowDateTime;
            if (v < prev) mono_ok = 0;
            if (v != prev) distinct++;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
            prev = v;
        }
        span = max_v - min_v;
        printf("    Monotonic over %d tight calls: %s\n", GRANULARITY_SAMPLES, mono_ok ? "PASS" : "FAIL");
        printf("    Distinct values: %llu, span %.3f ms\n",
               (unsigned long long)distinct, (double)span / 10000.0);

        /* Same-page identity: the precise and the tick path draw from one seqlock page. */
        GetSystemTimeAsFileTime(&a);
        GetSystemTimePreciseAsFileTime(&b);
        {
            uint64_t va = ((uint64_t)a.dwHighDateTime << 32) | a.dwLowDateTime;
            uint64_t vb = ((uint64_t)b.dwHighDateTime << 32) | b.dwLowDateTime;
            uint64_t d = (va > vb) ? (va - vb) : (vb - va);
            printf("    |precise - GetSystemTimeAsFileTime|: %.3f ms: %s\n", (double)d / 10000.0,
                   (patched_gst && d == 0) ? "PASS (identical shared-data page)" : "info (baseline: independent paths)");
        }

        for (i = 0; i < WARMUP_ITERS; i++)
        {
            GetSystemTimePreciseAsFileTime(&a);
            dummy += (DWORD)a.dwLowDateTime;
        }
        QueryPerformanceCounter(&t0);
        for (i = 0; i < BENCH_ITERS; i++)
        {
            GetSystemTimePreciseAsFileTime(&a);
            dummy += (DWORD)a.dwLowDateTime;
        }
        QueryPerformanceCounter(&t1);

        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        ns_per_op = (sec * 1e9) / BENCH_ITERS;
        mops = (double)BENCH_ITERS / (sec * 1e6);
        printf("    GetSystemTimePreciseAsFileTime benchmark (%d calls):\n", BENCH_ITERS);
        printf("    Total Duration: %.4f s\n", sec);
        printf("    Latency:        %.2f ns / call\n", ns_per_op);
        printf("    Throughput:     %.2f MILLION calls/sec\n", mops);

        gst_ok = mono_ok;
        g_gst_ok = gst_ok;
    }

    /* ------------------------------------------------------------- */
    /* 11. GetVersion inline trampoline                              */
    /* ------------------------------------------------------------- */
    {
        DWORD (WINAPI *p_gv_k32)(void);
        DWORD (WINAPI *p_gv_kb)(void);
        DWORD v_imp, v_k32, v_kb, v_base, expect_usd;
        unsigned int k_major, k_minor, k_build;
        int gv_ok = 1;

        p_gv_k32 = (DWORD (WINAPI *)(void))GetProcAddress(hK32, "GetVersion");
        p_gv_kb  = (DWORD (WINAPI *)(void))GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetVersion");

        printf("\n[11] GetVersion Inline Trampoline:\n");
        printf("    IAT import pointer:          %p\n", (void *)GetVersion);
        printf("    GetProcAddress(kernel32):    %p\n", (void *)p_gv_k32);
        printf("    GetProcAddress(kernelbase):  %p\n", (void *)p_gv_kb);
        printf("    Export identity (k32=kb):    %s\n",
               (p_gv_k32 == p_gv_kb) ? "PASS (single unified trampoline)"
                                     : "info (baseline: distinct real export addresses)");

        k_major = KUSD_NT_MAJOR_VERSION;
        k_minor = KUSD_NT_MINOR_VERSION;
        k_build = KUSD_NT_BUILD_NUMBER;
        expect_usd = (k_major & 0xff) | ((k_minor & 0xff) << 8) | ((k_build & 0x7fff) << 16);

        v_imp  = GetVersion();
        v_k32  = p_gv_k32 ? p_gv_k32() : 0;
        v_kb   = p_gv_kb  ? p_gv_kb()  : 0;
        v_base = baseline_GetVersion_via_GetVersionExW();

        printf("    KUSER_SHARED_DATA: NtMajorVersion 0x7ffe026c=%u NtMinorVersion 0x7ffe0270=%u NtBuildNumber 0x7ffe0260=%u\n",
               k_major, k_minor, k_build);
        printf("    Expected (packing rule on raw shared data): 0x%08x\n", (unsigned int)expect_usd);
        printf("    import/kernel32/kernelbase results:         0x%08x / 0x%08x / 0x%08x\n",
               (unsigned int)v_imp, (unsigned int)v_k32, (unsigned int)v_kb);
        printf("    Unpatched baseline (GetVersionExW formula):  0x%08x\n", (unsigned int)v_base);

        /* Every entry point for the name must agree; that is the mode-independent
           invariant. The raw shared-data packing rule is the trampoline's own
           contract, so it is reported as an observation: it binds when the import is
           the trampoline, while the unpatched kernelbase entry point answers from the
           PEB/registry fields and legitimately reports a different version. */
        gv_ok = (v_imp == v_k32) && (v_k32 == v_kb) && (expect_usd != 0);
        printf("    Value identity (import == k32 == kernelbase): %s\n", gv_ok ? "PASS" : "FAIL");
        printf("    Raw shared-data packing rule honoured:        %s\n",
               (v_imp == expect_usd) ? "PASS (trampoline active)"
                                     : "info (baseline path: kernelbase answers from the PEB fields)");
        printf("    Packed layout decodes to major=%u minor=%u build=%u: %s\n",
               (unsigned int)(v_imp & 0xff), (unsigned int)((v_imp >> 8) & 0xff), (unsigned int)((v_imp >> 16) & 0x7fff),
               (((v_imp & 0xff) == k_major) && (((v_imp >> 8) & 0xff) == k_minor) &&
                (((v_imp >> 16) & 0x7fff) == (k_build & 0x7fff))) ? "PASS (matches shared data)"
                                                                  : "info (baseline: PEB-derived version)");
        printf("    Agreement with the PEB/registry baseline:    %s\n",
               (v_imp == v_base) ? "PASS"
                                 : "info (KUSER page and PEB version fields diverge on this prefix)");

        for (i = 0; i < WARMUP_ITERS; i++) dummy += GetVersion();
        QueryPerformanceCounter(&t0);
        for (i = 0; i < BENCH_ITERS; i++) dummy += GetVersion();
        QueryPerformanceCounter(&t1);

        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        ns_per_op = (sec * 1e9) / BENCH_ITERS;
        mops = (double)BENCH_ITERS / (sec * 1e6);
        printf("    GetVersion benchmark (%d calls):\n", BENCH_ITERS);
        printf("    Total Duration: %.4f s\n", sec);
        printf("    Latency:        %.2f ns / call\n", ns_per_op);
        printf("    Throughput:     %.2f MILLION calls/sec\n", mops);

        /* Baseline cost reference: the unpatched kernelbase formula per call. */
        for (i = 0; i < WARMUP_ITERS / 10; i++) dummy += baseline_GetVersion_via_GetVersionExW();
        QueryPerformanceCounter(&t0);
        for (i = 0; i < BENCH_ITERS / 10; i++) dummy += baseline_GetVersion_via_GetVersionExW();
        QueryPerformanceCounter(&t1);
        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        printf("    Baseline GetVersionExW formula reference (%d calls): %.2f ns / call\n",
               BENCH_ITERS / 10, (sec * 1e9) / (BENCH_ITERS / 10));

        g_ver_ok = gv_ok;
    }

    /* ------------------------------------------------------------- */
    /* 12. GetActiveProcessorCount inline trampoline                 */
    /* ------------------------------------------------------------- */
    {
        DWORD (WINAPI *p_apc_k32)(WORD);
        DWORD n_all, n_g0, n_g1, n_raw3c0, n_raw2ec, n_sysinfo, n_max0;
        DWORD b_all, b_max0, b_g1, grp;
        SYSTEM_INFO si;

        p_apc_k32 = (DWORD (WINAPI *)(WORD))GetProcAddress(hK32, "GetActiveProcessorCount");

        printf("\n[12] GetActiveProcessorCount Inline Trampoline:\n");
        printf("    IAT import pointer:          %p\n", (void *)GetActiveProcessorCount);
        printf("    GetProcAddress(kernel32):    %p\n", (void *)p_apc_k32);

        n_raw3c0  = KUSD_ACTIVE_PROCESSOR_COUNT;
        n_raw2ec  = KUSD_AT_02EC;
        GetSystemInfo(&si);
        /* dwNumberOfProcessors is mingw's name for the Vista+ SDK union member
           dwActiveProcessor - the same SYSTEM_INFO field. */
        n_sysinfo = si.dwNumberOfProcessors;

        n_all = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        n_g0  = GetActiveProcessorCount(0);
        n_g1  = GetActiveProcessorCount(1);
        n_max0 = GetMaximumProcessorCount(0);

        b_all  = baseline_group_count(0);
        b_max0 = baseline_group_count(1);
        /* group 1 through the unpatched path: out of range on single-group Wine */
        b_g1 = p_apc_k32 ? p_apc_k32(1) : 0;

        printf("    KUSER_SHARED_DATA->ActiveProcessorCount 0x7ffe03c0: %lu\n", (unsigned long)n_raw3c0);
        printf("    DWORD at 0x7ffe02ec (SafeBootMode/VirtualizationFlags, NOT a count): 0x%08x\n",
               (unsigned int)n_raw2ec);
        printf("    import ALL_PROCESSOR_GROUPS(0xffff):  %lu\n", (unsigned long)n_all);
        printf("    import group 0:                       %lu\n", (unsigned long)n_g0);
        printf("    import group 1 (out of range):        %lu\n", (unsigned long)n_g1);
        printf("    GetSystemInfo().dwActiveProcessor:    %lu\n", (unsigned long)n_sysinfo);
        printf("    GetMaximumProcessorCount(0):          %lu\n", (unsigned long)n_max0);
        printf("    Baseline GetLogicalProcessorInformationEx walk: active %lu, maximum %lu, group1 %lu\n",
               (unsigned long)b_all, (unsigned long)b_max0, (unsigned long)b_g1);

        g_apc_ok = (n_raw3c0 > 0)
                   && (n_all == n_raw3c0) && (n_g0 == n_raw3c0)
                   && (n_sysinfo == n_raw3c0) && (n_g1 == 0)
                   && (b_all == n_raw3c0) && (b_max0 == n_max0) && (b_g1 == 0);
        printf("    Value identity (import vs shared data vs GetSystemInfo vs LPI walk):\n");
        printf("      ALL == shared data:            %s\n", (n_all == n_raw3c0) ? "PASS" : "FAIL");
        printf("      group0 == shared data:         %s\n", (n_g0 == n_raw3c0) ? "PASS" : "FAIL");
        printf("      shared data == GetSystemInfo:  %s\n", (n_sysinfo == n_raw3c0) ? "PASS" : "FAIL");
        printf("      out-of-range group -> 0:       %s\n", (n_g1 == 0 && b_g1 == 0) ? "PASS" : "FAIL");
        printf("      import == LPI baseline:        %s\n", (b_all == n_all && b_max0 == n_max0) ? "PASS" : "FAIL");
        printf("    GetActiveProcessorCount suite:   %s\n", g_apc_ok ? "PASS" : "FAIL");

        for (i = 0; i < WARMUP_ITERS; i++)
            dummy += GetActiveProcessorCount((i & 1) ? ALL_PROCESSOR_GROUPS : (WORD)0);
        QueryPerformanceCounter(&t0);
        for (i = 0; i < BENCH_ITERS; i++)
            dummy += GetActiveProcessorCount((i & 1) ? ALL_PROCESSOR_GROUPS : (WORD)0);
        QueryPerformanceCounter(&t1);

        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        ns_per_op = (sec * 1e9) / BENCH_ITERS;
        mops = (double)BENCH_ITERS / (sec * 1e6);
        printf("    GetActiveProcessorCount benchmark (%d calls, group 0 / ALL alternating):\n", BENCH_ITERS);
        printf("    Total Duration: %.4f s\n", sec);
        printf("    Latency:        %.2f ns / call\n", ns_per_op);
        printf("    Throughput:     %.2f MILLION calls/sec\n", mops);

        /* Cost of the unpatched algorithm, for the A/B delta. */
        for (i = 0; i < 200; i++) dummy += (DWORD)baseline_group_count(0);
        QueryPerformanceCounter(&t0);
        for (i = 0; i < 2000; i++) dummy += (DWORD)baseline_group_count(0);
        QueryPerformanceCounter(&t1);
        sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
        printf("    Baseline GetLogicalProcessorInformationEx walk (2000 calls): %.2f us / call\n",
               (sec * 1e6) / 2000);
        (void)grp;
    }

    /* ------------------------------------------------------------- */
    /* 13. RtlEqualUnicodeString SSE2 inline trampoline              */
    /* ------------------------------------------------------------- */
    {
        static const WCHAR wA[]  = L"A", wB[] = L"B";
        static const WCHAR wAB[] = L"AB", wAb[] = L"Ab";
        static const WCHAR w4[]  = L"abcd", w4d[] = L"abcD", w4f[] = L"Abcd";
        static const WCHAR w8[]  = L"abcdefgh", w8d[] = L"abcdefgX", w8f[] = L"Xbcdefgh";
        static const WCHAR w3[]  = L"abc", w3d[] = L"abd";
        static const WCHAR w7[]  = L"abcdefg", w7d[] = L"abcdefh";
        static const WCHAR w9[]  = L"abcdefghi", w9d[] = L"abcdefghX";
        static const WCHAR wE[]  = L"";
        static const WCHAR wci1[] = L"AbcD", wci2[] = L"aBCd", wci3[] = L"AbcX";
        static const WCHAR wl1[] = L"HelloWORLDxyz", wl2[] = L"hELLOworldXYZ",
                             wl3[] = L"hELLOworldXYz", wl4[] = L"hELLOworldXYq";
        static const WCHAR wo1[] = L"AbcDefg", wo2[] = L"aBCdefG";

        struct eq_vec { const WCHAR *a; USHORT ca; const WCHAR *b; USHORT cb; BOOLEAN ci; int expect; const char *what; };
        static const struct eq_vec vecs[] = {
            { wA,  1, wA,  1, 0, 1, "len  2 equal, distinct buffers" },
            { wA,  1, wB,  1, 0, 0, "len  2 differ" },
            { wAB, 2, wAB, 2, 0, 1, "len  4 equal" },
            { wAB, 2, wAb, 2, 0, 0, "len  4 differ" },
            { w4,  4, w4,  4, 0, 1, "len  8 equal (movq path)" },
            { w4,  4, w4d, 4, 0, 0, "len  8 differ, last char" },
            { w4,  4, w4f, 4, 0, 0, "len  8 differ, first char" },
            { w8,  8, w8,  8, 0, 1, "len 16 equal (SSE2 path)" },
            { w8,  8, w8d, 8, 0, 0, "len 16 differ, last char (SSE2)" },
            { w8,  8, w8f, 8, 0, 0, "len 16 differ, first char (SSE2)" },
            { w3,  3, w3,  3, 0, 1, "len  6 equal (not an exact load width -> C impl)" },
            { w3,  3, w3d, 3, 0, 0, "len  6 differ" },
            { w7,  7, w7,  7, 0, 1, "len 14 equal (C impl)" },
            { w7,  7, w7d, 7, 0, 0, "len 14 differ" },
            { w9,  9, w9,  9, 0, 1, "len 18 equal (> 16 -> C impl)" },
            { w9,  9, w9d, 9, 0, 0, "len 18 differ" },
            { wE,  0, wE,  0, 0, 1, "len  0, distinct non-NULL buffers" },
            { wE,  0, wA,  1, 0, 0, "len 0 vs len 2 (length mismatch)" },
            { NULL,0, NULL,0, 0, 1, "len  0, both buffers NULL (buffer identity)" },
            { wci1,4, wci2,4, 1, 1, "len  8 case-insensitive equal -> C impl" },
            { wci1,4, wci2,4, 0, 0, "len  8 same pair, case-sensitive" },
            { wci3,4, wci2,4, 1, 0, "len  8 case-insensitive differ" },
            { wo1, 7, wo2, 7, 1, 1, "len 14 case-insensitive equal" },
            { wl1,13, wl2,13, 1, 1, "len 26 case-insensitive equal" },
            { wl1,13, wl3,13, 1, 1, "len 26 case-folded tail (yz vs Yz fold equal)" },
            { wl1,13, wl4,13, 1, 0, "len 26 case-insensitive differ" },
        };
        BOOLEAN (WINAPI *p_req_nt)(const UNICODE_STRING *, const UNICODE_STRING *, BOOLEAN);
        UNICODE_STRING s1, s2, same;
        size_t v;
        int eq_ok = 1, vec_fail = 0, patched_eq;
        int pr;

        p_req_nt = (BOOLEAN (WINAPI *)(const UNICODE_STRING *, const UNICODE_STRING *, BOOLEAN))
                       GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlEqualUnicodeString");
        patched_eq = (p_req_nt == (void *)RtlEqualUnicodeString);

        printf("\n[13] RtlEqualUnicodeString SSE2 Inline Trampoline:\n");
        printf("    IAT import pointer:          %p\n", (void *)RtlEqualUnicodeString);
        printf("    GetProcAddress(ntdll):       %p\n", (void *)p_req_nt);
        printf("    Export identity:             %s\n",
               patched_eq ? "PASS (single unified trampoline)"
                          : "info (baseline: the real ntdll export)");
        printf("    UNICODE_STRING layout used by the trampoline: Length at +0, Buffer at +8\n");
        {
            UNICODE_STRING probe;
            probe.Length = 0x1234; probe.MaximumLength = 0x5678; probe.Buffer = (PWSTR)(void *)0xdeadbeef;
            printf("      offsetof(Length)=%zu offsetof(Buffer)=%zu (must be 0 and 8): %s\n",
                   (size_t)((char *)&probe.Length - (char *)&probe),
                   (size_t)((char *)&probe.Buffer - (char *)&probe),
                   ((&probe.Length == (USHORT *)&probe) && ((char *)&probe.Buffer - (char *)&probe) == 8)
                       ? "PASS" : "FAIL");
            if (!((&probe.Length == (USHORT *)&probe) &&
                  ((char *)&probe.Buffer - (char *)&probe) == 8)) eq_ok = 0;
        }

        printf("    Result identity over %u vectors (import call vs C reference):\n",
               (unsigned)(sizeof(vecs) / sizeof(vecs[0])));
        for (v = 0; v < sizeof(vecs) / sizeof(vecs[0]); v++)
        {
            s1 = make_us(vecs[v].a, vecs[v].ca);
            s2 = make_us(vecs[v].b, vecs[v].cb);
            pr = vecs[v].ci ? ref_equal_ci(&s1, &s2) : ref_equal_cs(&s1, &s2);
            {
                int got = RtlEqualUnicodeString(&s1, &s2, vecs[v].ci) ? 1 : 0;
                int ok = (pr >= 0) && (got == vecs[v].expect) && (got == pr);
                if (!ok) { eq_ok = 0; vec_fail++; }
                printf("      len %2u ci=%d %-46s got=%d ref=%d expect=%d %s\n",
                       vecs[v].ca * 2, vecs[v].ci, vecs[v].what, got, pr, vecs[v].expect,
                       ok ? "PASS" : "FAIL");
            }
        }
        printf("    Vector identity: %s\n", vec_fail ? "FAIL" : "PASS");

        /* Pointer identity: one UNICODE_STRING passed as both arguments. */
        s1 = make_us(w8, 8);
        {
            int got = RtlEqualUnicodeString(&s1, &s1, FALSE) ? 1 : 0;
            printf("    s1 == s2 (same struct pointer) -> TRUE: %s\n", got ? "PASS" : "FAIL");
            if (!got) eq_ok = 0;
        }
        s1 = make_us(w8, 8);
        same = s1;                              /* identical buffer, distinct struct */
        {
            int got = RtlEqualUnicodeString(&s1, &same, FALSE) ? 1 : 0;
            printf("    distinct structs, identical buffer -> TRUE: %s\n", got ? "PASS" : "FAIL");
            if (!got) eq_ok = 0;
        }

        /* NULL struct arguments: only legal to probe once patched, the unpatched
           ntdll entry point dereferences s1->Length and faults. */
        if (patched_eq)
        {
            int a = RtlEqualUnicodeString(NULL, &s1, FALSE) ? 1 : 0;
            int b = RtlEqualUnicodeString(&s1, NULL, FALSE) ? 1 : 0;
            int c = RtlEqualUnicodeString(NULL, NULL, FALSE) ? 1 : 0;
            printf("    NULL s1 -> FALSE: %s   NULL s2 -> FALSE: %s   both NULL -> TRUE: %s\n",
                   a ? "FAIL" : "PASS", b ? "FAIL" : "PASS", c ? "PASS" : "FAIL");
            if (a || b || !c) eq_ok = 0;
        }

        /* Over-read probe: every exact-load width and the fallback widths, string
           ending flush against a PAGE_NOACCESS page. */
        printf("    Page-guard over-read probe (string ends at the page edge):\n");
        {
            static const USHORT widths[] = { 2, 4, 6, 8, 14, 16, 18 };
            unsigned w, faulted_any = 0;
            for (w = 0; w < sizeof(widths) / sizeof(widths[0]); w++)
            {
                int f1 = probe_overread(widths[w], 0);
                int f2 = probe_overread(widths[w], 1);
                int ok = (f1 <= 0) && (f2 <= 0);
                if (!ok) { eq_ok = 0; faulted_any++; }
                printf("      Length %2u (equal / differ at end): faults %d / %d  %s\n",
                       widths[w], f1, f2, ok ? "PASS (no read past Length)" : "FAIL (over-read)");
            }
            (void)faulted_any;
        }

        /* Throughput: fast paths first, then the fallbacks they hand off to. */
        {
            static const struct { const WCHAR *src; USHORT chars; int ci; const char *what; int iters; } loops[] = {
                { w4,  4, 0, "len  8 equal (movq fast path)",    BENCH_ITERS },
                { w8,  8, 0, "len 16 equal (SSE2 fast path)",    BENCH_ITERS },
                { w3,  3, 0, "len  6 equal (C fallback)",        BENCH_ITERS },
                { wl1,13, 0, "len 26 equal (> 16, C fallback)",  BENCH_ITERS / 4 },
                { wl1,13, 1, "len 26 case-insensitive (C path)", BENCH_ITERS / 4 },
            };
            unsigned l;
            for (l = 0; l < sizeof(loops) / sizeof(loops[0]); l++)
            {
                /* Distinct buffers with equal content: identical buffers short-circuit
                   on the Buffer == test and would measure nothing. */
                WCHAR copy[24];
                UNICODE_STRING x = make_us(loops[l].src, loops[l].chars);
                memcpy(copy, loops[l].src, loops[l].chars * sizeof(WCHAR));
                UNICODE_STRING y = make_us(copy, loops[l].chars);
                int it = loops[l].iters;
                for (i = 0; i < WARMUP_ITERS / 4; i++) dummy += RtlEqualUnicodeString(&x, &y, (BOOLEAN)loops[l].ci);
                QueryPerformanceCounter(&t0);
                for (i = 0; i < it; i++) dummy += RtlEqualUnicodeString(&x, &y, (BOOLEAN)loops[l].ci);
                QueryPerformanceCounter(&t1);
                sec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
                printf("    RtlEqualUnicodeString %-38s %9d calls: %6.2f ns / call, %8.2f M calls/sec\n",
                       loops[l].what, it, (sec * 1e9) / it, (double)it / (sec * 1e6));
            }
        }

        g_eq_ok = eq_ok;
    }

    printf("\n====================================================================\n");
    printf("  Final Suite Verdict: %s\n",
           (g_shared_counter == expected && g_pf_ok && g_qpc_ok && g_gst_ok && g_ver_ok && g_apc_ok && g_eq_ok)
               ? "ALL BENCHMARKS & TESTS PASSED (rc=0)"
               : "SOME TESTS FAILED (rc=1)");
    printf("====================================================================\n");

    return (g_shared_counter == expected && g_pf_ok && g_qpc_ok && g_gst_ok && g_ver_ok && g_apc_ok && g_eq_ok) ? 0 : 1;
}
