/*
 * Delay-load failure semantics conformance tests.
 *
 * Copyright 2026 Wine Foundation
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winerror.h"
#include "wine/test.h"
#include "delayloadhandler.h"

static void * (WINAPI *pLdrResolveDelayLoadedAPI)( void *base, const IMAGE_DELAYLOAD_DESCRIPTOR *desc,
                                                   PDELAYLOAD_FAILURE_DLL_CALLBACK dllhook,
                                                   PDELAYLOAD_FAILURE_SYSTEM_ROUTINE syshook,
                                                   IMAGE_THUNK_DATA *thunk, ULONG flags );

#define MISSING_DLL "wine-test-delayload-missing.dll"
#define MISSING_FN  "MissingFunctionInMissingDll"
#define FAKE_FN     ((void *)0xdeadbeef)
#define FAKE_FN2    ((void *)0xc0ffee)
#define FAKE_MOD    ((HMODULE)0x1140341)

/* dliNotify/dliFailure ids from the MSVC delay-load helper (delayimp.h) */
#define DLI_NOTE_PRE_LOAD_LIBRARY 3
#define DLI_GET_PROC_ADDR         4

static int hook_calls, sys_calls;
static ULONG hook_code[8];
static DELAYLOAD_INFO hook_info[8];
static void *hook_result_3, *hook_result_4;
static char sys_dll[128], sys_fn[128];
static int seen_3, seen_4;

typedef struct
{
    IMAGE_DELAYLOAD_DESCRIPTOR desc;
    IMAGE_THUNK_DATA intbl[2];
    IMAGE_THUNK_DATA iat[2];
    HMODULE mod;
    char dllname[64];
    struct { WORD hint; char name[64]; } byname;
} dl_block;

static dl_block block;

static void setup_block( const char *dll )
{
    memset( &block, 0, sizeof(block) );
    strcpy( block.dllname, dll );
    block.byname.hint = 0;
    strcpy( block.byname.name, MISSING_FN );

    block.desc.DllNameRVA = (DWORD)(UINT_PTR)(block.dllname - (char *)&block);
    block.desc.ModuleHandleRVA = (DWORD)(UINT_PTR)((char *)&block.mod - (char *)&block);
    block.desc.ImportNameTableRVA = (DWORD)(UINT_PTR)((char *)&block.intbl - (char *)&block);
    block.desc.ImportAddressTableRVA = (DWORD)(UINT_PTR)((char *)&block.iat - (char *)&block);
    block.desc.BoundImportAddressTableRVA = 0;
    block.desc.UnloadInformationTableRVA = 0;

    block.intbl[0].u1.AddressOfData = (DWORD)(UINT_PTR)((char *)&block.byname - (char *)&block);
    block.intbl[1].u1.Ordinal = IMAGE_ORDINAL_FLAG | 2;
}

static void reset_state( void )
{
    hook_calls = sys_calls = seen_3 = seen_4 = 0;
    hook_result_3 = NULL;
    hook_result_4 = NULL;
    sys_dll[0] = sys_fn[0] = 0;
    memset( hook_code, 0, sizeof(hook_code) );
    memset( hook_info, 0, sizeof(hook_info) );
    block.mod = NULL;
    block.iat[0].u1.Function = block.iat[1].u1.Function = 0;
}

static void * WINAPI failure_hook( ULONG code, PDELAYLOAD_INFO info )
{
    ok( code == DLI_NOTE_PRE_LOAD_LIBRARY || code == DLI_GET_PROC_ADDR,
        "unexpected failure-hook notification %u\n", code );
    ok( info->Size == sizeof(*info), "DELAYLOAD_INFO.Size %u != %u\n", info->Size, (ULONG)sizeof(*info) );
    ok( !strcmp( info->TargetDllName, block.dllname ), "unexpected TargetDllName %s\n",
        debugstr_a( info->TargetDllName ) );
    ok( info->ThunkAddress == &block.iat[0] || info->ThunkAddress == &block.iat[1],
        "unexpected ThunkAddress %p\n", info->ThunkAddress );

    if (code == DLI_NOTE_PRE_LOAD_LIBRARY)
    {
        seen_3 = 1;
        hook_code[hook_calls] = code;
        hook_info[hook_calls] = *info;
        hook_calls++;
        return hook_result_3;
    }
    seen_4 = 1;
    hook_code[hook_calls] = code;
    hook_info[hook_calls] = *info;
    hook_calls++;
    return hook_result_4;
}

static void * WINAPI system_routine( LPCSTR dll, LPCSTR fn )
{
    sys_calls++;
    lstrcpynA( sys_dll, dll, sizeof(sys_dll) );
    if ((ULONG_PTR)fn >> 16) lstrcpynA( sys_fn, fn, sizeof(sys_fn) );
    else sprintf( sys_fn, "#%u", (unsigned int)(DWORD_PTR)fn );
    return FAKE_FN2;
}

/* The failure hook of a .NET Native mrt100_app.dll style image reads
 * TargetApiDescriptor.Description.Name as a string for imports by name:
 * it must be the import name, not the ordinal word. */
static void test_resolve_missing_dll_byname( void )
{
    static const char *expect_fn = MISSING_FN;
    void *ret;

    setup_block( MISSING_DLL );
    reset_state();
    hook_result_4 = FAKE_FN;

    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, failure_hook, system_routine, &block.iat[0], 0 );

    ok( seen_3, "failure hook was not notified with dliNotePreLoadLibrary (3)\n" );
    ok( seen_4, "failure hook was not notified with dliGetProcAddr (4)\n" );
    ok( hook_calls == (seen_3 ? 2 : 1), "hook called %u times, expected %u\n", hook_calls, seen_3 ? 2u : 1u );
    ok( !sys_calls, "system routine called %u times, hook accepted the failure\n", sys_calls );
    ok( ret == FAKE_FN, "unexpected return %p\n", ret );

    /* the last hook call is dliGetProcAddr with the populated descriptor */
    {
        PDELAYLOAD_INFO info = &hook_info[hook_calls - 1];
        ok( hook_code[hook_calls - 1] == DLI_GET_PROC_ADDR, "last notification %u, expected 4\n",
            hook_code[hook_calls - 1] );
        ok( info->TargetApiDescriptor.ImportDescribedByName, "import was not described by name\n" );
        if (info->TargetApiDescriptor.ImportDescribedByName)
        {
            /* On the broken code Description.Ordinal carries the RVA word: a wild pointer.
             * Guard before dereferencing. */
            if ((ULONG_PTR)info->TargetApiDescriptor.Description.Name > 0x10000)
                ok( !strcmp( info->TargetApiDescriptor.Description.Name, expect_fn ),
                    "Description.Name %s, expected %s\n",
                    debugstr_a( info->TargetApiDescriptor.Description.Name ), expect_fn );
            else
                ok( FALSE, "Description.Name is not a pointer: %p\n",
                    (void *)(ULONG_PTR)info->TargetApiDescriptor.Description.Name );
        }
        ok( info->LastError == ERROR_MOD_NOT_FOUND, "LastError 0x%08lx, expected ERROR_MOD_NOT_FOUND\n",
            info->LastError );
    }

    /* now decline in the hook: the system routine must run, as on Windows */
    setup_block( MISSING_DLL );
    reset_state();

    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, NULL, system_routine, &block.iat[0], 0 );
    ok( sys_calls == 1, "system routine called %u times, expected 1\n", sys_calls );
    ok( ret == FAKE_FN2, "unexpected return %p\n", ret );
    ok( !strcmp( sys_dll, MISSING_DLL ), "system routine dll %s\n", debugstr_a( sys_dll ) );
    ok( !strcmp( sys_fn, MISSING_FN ), "system routine fn %s\n", debugstr_a( sys_fn ) );

    setup_block( MISSING_DLL );
    reset_state();
    hook_result_4 = NULL; /* decline */
    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, failure_hook, system_routine, &block.iat[0], 0 );
    ok( sys_calls == 1, "declining dll hook must fall through to the system routine, sys_calls=%u\n", sys_calls );
    ok( ret == FAKE_FN2, "unexpected return %p\n", ret );
}

static void test_resolve_missing_dll_byordinal( void )
{
    void *ret;

    setup_block( MISSING_DLL );
    reset_state();

    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, NULL, system_routine, &block.iat[1], 0 );
    ok( sys_calls == 1, "system routine called %u times, expected 1\n", sys_calls );
    ok( ret == FAKE_FN2, "unexpected return %p\n", ret );
    ok( !strcmp( sys_dll, MISSING_DLL ), "system routine dll %s\n", debugstr_a( sys_dll ) );
    ok( !strcmp( sys_fn, "#2" ), "system routine fn %s, expected #2\n", debugstr_a( sys_fn ) );

    setup_block( MISSING_DLL );
    reset_state();
    hook_result_4 = FAKE_FN;
    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, failure_hook, system_routine, &block.iat[1], 0 );
    ok( ret == FAKE_FN, "unexpected return %p\n", ret );
    {
        PDELAYLOAD_INFO info = &hook_info[hook_calls - 1];
        ok( !info->TargetApiDescriptor.ImportDescribedByName, "import was described by name\n" );
        ok( info->TargetApiDescriptor.Description.Ordinal == 2, "Description.Ordinal %u, expected 2\n",
            (unsigned int)info->TargetApiDescriptor.Description.Ordinal );
    }
}

/* dll loads, function missing: LastError must be ERROR_PROC_NOT_FOUND */
static void test_resolve_missing_function( void )
{
    void *ret;

    setup_block( "kernel32.dll" );
    reset_state();
    hook_result_4 = FAKE_FN;

    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, failure_hook, system_routine, &block.iat[0], 0 );
    ok( ret == FAKE_FN, "unexpected return %p\n", ret );
    ok( block.mod != NULL, "module handle was not set\n" );
    {
        PDELAYLOAD_INFO info = &hook_info[hook_calls - 1];
        ok( info->LastError == ERROR_PROC_NOT_FOUND, "LastError 0x%08lx, expected ERROR_PROC_NOT_FOUND\n",
            info->LastError );
        ok( !strcmp( info->TargetDllName, "kernel32.dll" ), "TargetDllName %s\n",
            debugstr_a( info->TargetDllName ) );
    }
}

/*
 * The default handler (kernelbase DelayLoadFailureHook) must raise Windows'
 * ERROR_DELAY_LOAD_FAILED (0x505), non-continuable, with (dll, function)
 * parameters: .NET Native's mrt100_app.dll __except filter tests for exactly
 * this code to take its built-in fallback. Wine raised EXCEPTION_WINE_STUB.
 *
 * The exception is non-continuable, so it is observed from a child process
 * through a vectored handler: 0xa11c5 = code+args match, 0xdead = mismatch.
 */
#define CHILD_OK    0xa11c5
#define CHILD_BAD   0xdead

static LONG WINAPI child_veh( PEXCEPTION_POINTERS eps )
{
    PEXCEPTION_RECORD rec = eps->ExceptionRecord;
    if (rec->ExceptionCode == ERROR_DELAY_LOAD_FAILED
        && rec->NumberParameters >= 2
        && rec->ExceptionInformation[0] > 0x10000
        && !strcmp( (const char *)rec->ExceptionInformation[0], MISSING_DLL )
        && rec->ExceptionInformation[1] > 0x10000
        && !strcmp( (const char *)rec->ExceptionInformation[1], MISSING_FN ))
        ExitProcess( CHILD_OK );
    ExitProcess( CHILD_BAD + (rec->ExceptionCode & 0xffffff) );
    return EXCEPTION_CONTINUE_SEARCH;
}

static void run_child_mode( void )
{
    FARPROC (WINAPI *pDelayLoadFailureHook)( LPCSTR, LPCSTR );
    HMODULE hkernel32 = GetModuleHandleA( "kernel32" );
    void *ret;

    pDelayLoadFailureHook = (void *)GetProcAddress( hkernel32, "DelayLoadFailureHook" );
    if (!pDelayLoadFailureHook) ExitProcess( CHILD_BAD );

    setup_block( MISSING_DLL );
    reset_state();
    AddVectoredExceptionHandler( 1, child_veh );

    /* no dll hook: the system routine (the default handler) runs */
    ret = pLdrResolveDelayLoadedAPI( &block, &block.desc, NULL, (void *)pDelayLoadFailureHook, &block.iat[0], 0 );
    /* the raise is non-continuable: unreachable on success */
    ok( ret == NULL, "unexpected return %p\n", ret );
    ExitProcess( CHILD_BAD );
}

static void test_default_handler( void )
{
    STARTUPINFOA si;
    PROCESS_INFORMATION info;
    char path[MAX_PATH], cmdline[MAX_PATH + 16];
    DWORD code = 0;
    BOOL ret;

    sprintf( cmdline, "\"%s\" loader", GetModuleFileNameA( NULL, path, ARRAY_SIZE(path) ) ? path : "ntdll_test.exe" );
    SetEnvironmentVariableA( "WINE_P49_CHILD", "1" );
    memset( &si, 0, sizeof(si) );
    si.cb = sizeof(si);
    memset( &info, 0, sizeof(info) );
    ret = CreateProcessA( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &info );
    SetEnvironmentVariableA( "WINE_P49_CHILD", NULL );
    ok( ret, "CreateProcess failed, error %lu\n", GetLastError() );
    if (!ret) return;
    WaitForSingleObject( info.hProcess, 60000 );
    GetExitCodeProcess( info.hProcess, &code );
    CloseHandle( info.hProcess );
    CloseHandle( info.hThread );
    ok( code == CHILD_OK, "child exit code 0x%lx, expected 0x%x (ERROR_DELAY_LOAD_FAILED 0x505 raised with dll+fn args)\n",
        code, CHILD_OK );
}

/*
 * Explicit-path builtin redirection.
 *
 * Chromium/WebView2 probes its side-by-side directory by explicit path:
 * "<exe dir>\WebView2\dcomp.dll". The probe file is absent, and an explicit
 * path gets no system directory fallback, so the load fails outright with
 * ERROR_MOD_NOT_FOUND even though a Wine builtin of the same base name is
 * installed - our builtins are --wine-builtin PEs and cannot be staged into
 * a SxS directory as native files.
 *
 * The loader must redirect a failed explicit-path probe to the builtin with
 * the same base name (base name matched case-insensitively), and the loaded
 * module must be the very module a normal builtin load produces, so that
 * GetModuleHandleA of the bare name finds it and FreeLibrary releases it.
 * A base name that is not a Wine builtin must keep failing: the redirection
 * never masks an existing native file.
 */
static void test_sxs_path_builtin_redirect( void )
{
    char tmp[MAX_PATH], dir[MAX_PATH], sxs[MAX_PATH], path[MAX_PATH];
    HMODULE mod, ref;
    FARPROC proc;

    if (!winetest_platform_is_wine)
    {
        winetest_push_context( "sxs-redirect" );
        skip( "Wine loader builtin redirection, not applicable on Windows\n" );
        winetest_pop_context();
        return;
    }

    if (!GetTempPathA( ARRAY_SIZE(tmp), tmp ) || !tmp[0])
    {
        winetest_push_context( "sxs-redirect" );
        skip( "no temp path\n" );
        winetest_pop_context();
        return;
    }

    /* the SxS directory exists, the probed file does not: that is exactly what
     * the Chromium SxS probe looks like in Wine */
    sprintf( dir, "%swine-sxs-probe-%lu", tmp, GetCurrentProcessId() );
    CreateDirectoryA( dir, NULL );
    sprintf( sxs, "%s\\WebView2", dir );
    CreateDirectoryA( sxs, NULL );
    if (GetFileAttributesA( sxs ) == INVALID_FILE_ATTRIBUTES)
    {
        skip( "cannot create %s\n", sxs );
        return;
    }

    /* the redirection target has to be installed in this prefix */
    if (GetFileAttributesA( "C:\\windows\\system32\\kernelbase.dll" ) == INVALID_FILE_ATTRIBUTES)
    {
        winetest_push_context( "sxs-redirect" );
        skip( "no builtin file in the system directory\n" );
        winetest_pop_context();
        RemoveDirectoryA( sxs );
        RemoveDirectoryA( dir );
        return;
    }

    winetest_push_context( "sxs-redirect" );

    /* an already loaded builtin: the probe must yield the existing module */
    sprintf( path, "%s\\kernelbase.dll", sxs );
    ref = GetModuleHandleA( "kernelbase.dll" );
    ok( ref != NULL, "kernelbase is not loaded\n" );
    mod = LoadLibraryA( path );
    ok( mod != NULL, "LoadLibraryA(%s) failed, error %lu\n", path, GetLastError() );
    ok( mod == ref, "SxS probe module %p != kernelbase module %p\n", mod, ref );
    if (mod)
    {
        BOOL freed = FreeLibrary( mod );
        ok( freed, "FreeLibrary(%p) failed, error %lu\n", mod, GetLastError() );
    }

    /* base name matching is case-insensitive */
    sprintf( path, "%s\\KERNELBASE.DLL", sxs );
    mod = LoadLibraryA( path );
    ok( mod == ref, "case-insensitive SxS probe module %p != kernelbase module %p\n", mod, ref );
    if (mod) FreeLibrary( mod );

    /* a builtin that is not loaded yet: the probe must produce a full builtin
     * load, with the exports of the builtin available */
    sprintf( path, "%s\\version.dll", sxs );
    mod = LoadLibraryA( path );
    ok( mod != NULL, "LoadLibraryA(%s) failed, error %lu\n", path, GetLastError() );
    if (mod)
    {
        ok( mod == GetModuleHandleA( "version.dll" ),
            "SxS probe module %p != version module %p\n", mod, GetModuleHandleA( "version.dll" ) );
        proc = GetProcAddress( mod, "GetFileVersionInfoA" );
        ok( proc != NULL, "no GetFileVersionInfoA in %p\n", mod );
        FreeLibrary( mod );
    }

    /* a base name that is not a Wine builtin keeps failing */
    sprintf( path, "%s\\wine-sxs-probe-missing.dll", sxs );
    mod = LoadLibraryA( path );
    ok( mod == NULL, "LoadLibraryA(%s) returned %p, expected NULL\n", path, mod );
    ok( GetLastError() == ERROR_MOD_NOT_FOUND, "error %lu, expected ERROR_MOD_NOT_FOUND\n",
        GetLastError() );

    winetest_pop_context();

    RemoveDirectoryA( sxs );
    RemoveDirectoryA( dir );
}

START_TEST( loader )
{
    char **argv;
    int argc = winetest_get_mainargs( &argv );
    HMODULE hntdll = GetModuleHandleA( "ntdll.dll" );
    BOOL child = GetEnvironmentVariableA( "WINE_P49_CHILD", NULL, 0 ) > 0;

    if (child || (argc > 1 && !strcmp( argv[1], "--dlchild" )))
    {
        pLdrResolveDelayLoadedAPI = (void *)GetProcAddress( hntdll, "LdrResolveDelayLoadedAPI" );
        if (!pLdrResolveDelayLoadedAPI) { winetest_push_context( "child" ); winetest_skip( "no LdrResolveDelayLoadedAPI" ); winetest_pop_context(); return; }
        run_child_mode();
        return;
    }

    test_sxs_path_builtin_redirect();

    pLdrResolveDelayLoadedAPI = (void *)GetProcAddress( hntdll, "LdrResolveDelayLoadedAPI" );
    if (!pLdrResolveDelayLoadedAPI)
    {
        winetest_skip( "LdrResolveDelayLoadedAPI" );
        return;
    }

    winetest_push_context( "byname" );
    test_resolve_missing_dll_byname();
    winetest_pop_context();

    winetest_push_context( "byordinal" );
    test_resolve_missing_dll_byordinal();
    winetest_pop_context();

    winetest_push_context( "missingfn" );
    test_resolve_missing_function();
    winetest_pop_context();

    winetest_push_context( "default" );
    test_default_handler();
    winetest_pop_context();
}
