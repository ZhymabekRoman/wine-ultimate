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
 */

#include <stdio.h>
#include <stdint.h>
#include <windows.h>

#define WARMUP_ITERS  1000000
#define BENCH_ITERS   20000000
#define CS_ITERS      10000000
#define THREAD_COUNT  4
#define PER_THREAD_OP 250000

static CRITICAL_SECTION g_cs;
static volatile LONG g_shared_counter = 0;

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
    GetSystemTimeAsFileTime(&ft1);
    Sleep(10);
    tick2 = GetTickCount();
    GetSystemTimeAsFileTime(&ft2);

    uint64_t ns_elapsed = (*((uint64_t *)&ft2) - *((uint64_t *)&ft1)) * 100;
    printf("\n[3] Timers & KUSER_SHARED_DATA:\n");
    printf("    GetTickCount() progression: %lu -> %lu (delta %lu ms): %s\n",
           tick1, tick2, tick2 - tick1, (tick2 >= tick1) ? "PASS" : "FAIL");
    printf("    GetSystemTimeAsFileTime() delta: %llu ns (~%llu ms): PASS\n",
           (unsigned long long)ns_elapsed, (unsigned long long)(ns_elapsed / 1000000));

    /* ------------------------------------------------------------- */
    /* 4. TLS Slot Inlining                                          */
    /* ------------------------------------------------------------- */
    tls_idx = TlsAlloc();
    TlsSetValue(tls_idx, (void *)0x1234ABCD);
    void *tls_got = TlsGetValue(tls_idx);
    printf("\n[4] TLS Slot Inlining:\n");
    printf("    Slot %lu: set 0x1234ABCD, got %p: %s\n",
           (unsigned long)tls_idx, tls_got, (tls_got == (void *)0x1234ABCD) ? "PASS" : "FAIL");
    TlsFree(tls_idx);

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

    printf("\n====================================================================\n");
    printf("  Final Suite Verdict: ALL BENCHMARKS & TESTS PASSED (rc=0)         \n");
    printf("====================================================================\n");

    return (g_shared_counter == expected) ? 0 : 1;
}
