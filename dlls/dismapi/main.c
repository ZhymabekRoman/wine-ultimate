/*
 * DismApi implementation
 *
 * Copyright 2026 Zhymabek Roman
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
#include <stdlib.h>
#include <wchar.h>

#include "dismapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dismapi);

#define FEATURE_PWSH      L"Microsoft-PowerShell-Core"
#define FEATURE_MONO      L"NetFx4-WineMono"
#define FEATURE_NETFX4    L"NetFx4"
#define FEATURE_APPX      L"Appx-Deployment-Service"
#define FEATURE_APPXSVC   L"AppXSvc"

#define MAX_SESSIONS 32

struct session_entry {
    BOOL in_use;
    WCHAR image_path[MAX_PATH];
};

static struct session_entry g_sessions[MAX_SESSIONS];
static CRITICAL_SECTION g_cs;
static CRITICAL_SECTION_DEBUG g_cs_debug =
{
    0, 0, &g_cs,
    { &g_cs_debug.ProcessLocksList, &g_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": g_cs") }
};
static CRITICAL_SECTION g_cs = { &g_cs_debug, -1, 0, 0, 0, 0 };

static LONG g_init_count = 0;
static DismLogLevel g_log_level = DismLogErrorsWarningsInfo;
static WCHAR g_log_path[MAX_PATH] = {0};
static WCHAR g_scratch_dir[MAX_PATH] = {0};

static BOOL is_valid_session(DismSession session)
{
    BOOL valid = FALSE;
    if (session == 0 || session > MAX_SESSIONS) return FALSE;
    EnterCriticalSection(&g_cs);
    valid = g_sessions[session - 1].in_use;
    LeaveCriticalSection(&g_cs);
    return valid;
}

/* ========================================================================= */
/* PowerShell feature implementation                                         */
/* ========================================================================= */

static BOOL is_pwsh_enabled(void)
{
    WCHAR path[MAX_PATH];
    if (ExpandEnvironmentStringsW(L"%ProgramFiles%\\PowerShell\\7\\pwsh.exe", path, MAX_PATH))
    {
        DWORD attr = GetFileAttributesW(path);
        return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    }
    return FALSE;
}

static BOOL copy_or_hardlink_tree(const WCHAR *src, const WCHAR *dst)
{
    WIN32_FIND_DATAW fd;
    WCHAR src_search[MAX_PATH];
    WCHAR src_child[MAX_PATH];
    WCHAR dst_child[MAX_PATH];
    HANDLE hFind;

    CreateDirectoryW(dst, NULL);

    swprintf(src_search, MAX_PATH, L"%s\\*", src);
    hFind = FindFirstFileW(src_search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;

        swprintf(src_child, MAX_PATH, L"%s\\%s", src, fd.cFileName);
        swprintf(dst_child, MAX_PATH, L"%s\\%s", dst, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            copy_or_hardlink_tree(src_child, dst_child);
        }
        else
        {
            if (!CreateHardLinkW(dst_child, src_child, NULL))
                CopyFileW(src_child, dst_child, FALSE);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return TRUE;
}

static BOOL remove_directory_tree(const WCHAR *dir)
{
    WIN32_FIND_DATAW fd;
    WCHAR search[MAX_PATH];
    WCHAR child[MAX_PATH];
    HANDLE hFind;

    swprintf(search, MAX_PATH, L"%s\\*", dir);
    hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;

        swprintf(child, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            remove_directory_tree(child);
        else
        {
            SetFileAttributesW(child, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(child);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return RemoveDirectoryW(dir);
}

static void append_to_user_path(const WCHAR *dir)
{
    HKEY hkey;
    WCHAR path[4096] = {0};
    DWORD type = REG_EXPAND_SZ, size = sizeof(path);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0,
                        KEY_READ | KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hkey, L"Path", NULL, &type, (BYTE *)path, &size) == ERROR_SUCCESS)
        {
            if (!wcsstr(path, dir))
            {
                if (path[0] && path[lstrlenW(path) - 1] != L';')
                    lstrcatW(path, L";");
                lstrcatW(path, dir);
                RegSetValueExW(hkey, L"Path", 0, type, (BYTE *)path, (lstrlenW(path) + 1) * sizeof(WCHAR));
            }
        }
        else
        {
            lstrcpyW(path, dir);
            RegSetValueExW(hkey, L"Path", 0, REG_EXPAND_SZ, (BYTE *)path, (lstrlenW(path) + 1) * sizeof(WCHAR));
        }
        RegCloseKey(hkey);
    }
}

static void remove_from_user_path(const WCHAR *dir)
{
    HKEY hkey;
    WCHAR path[4096] = {0};
    WCHAR new_path[4096] = {0};
    DWORD type = REG_EXPAND_SZ, size = sizeof(path);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_WRITE, &hkey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hkey, L"Path", NULL, &type, (BYTE *)path, &size) == ERROR_SUCCESS)
        {
            WCHAR *tok, *context;
            WCHAR *p = path;
            BOOL first = TRUE;

            while ((tok = wcstok_s(p, L";", &context)))
            {
                p = NULL;
                if (wcsicmp(tok, dir) != 0)
                {
                    if (!first) lstrcatW(new_path, L";");
                    lstrcatW(new_path, tok);
                    first = FALSE;
                }
            }
            RegSetValueExW(hkey, L"Path", 0, type, (BYTE *)new_path, (lstrlenW(new_path) + 1) * sizeof(WCHAR));
        }
        RegCloseKey(hkey);
    }
}

static BOOL enable_pwsh(PCWSTR *SourcePaths, UINT SourcePathCount, HANDLE cancel_event, DISM_PROGRESS_CALLBACK progress, PVOID user_context)
{
    WCHAR dst[MAX_PATH];
    const WCHAR *src = NULL;
    static const WCHAR * const candidates[] = {
        L"Z:\\var\\tmp\\pwsh-win-x64",
        L"\\\\?\\unix\\var\\tmp\\pwsh-win-x64",
        L"C:\\var\\tmp\\pwsh-win-x64",
    };
    UINT i;
    HKEY hkey;

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    /* Check caller-supplied source paths first */
    if (SourcePaths && SourcePathCount > 0)
    {
        for (i = 0; i < SourcePathCount; i++)
        {
            if (SourcePaths[i] && GetFileAttributesW(SourcePaths[i]) != INVALID_FILE_ATTRIBUTES)
            {
                src = SourcePaths[i];
                break;
            }
        }
    }

    /* Fallback to default candidates */
    if (!src)
    {
        for (i = 0; i < ARRAY_SIZE(candidates); i++)
        {
            if (GetFileAttributesW(candidates[i]) != INVALID_FILE_ATTRIBUTES)
            {
                src = candidates[i];
                break;
            }
        }
    }

    if (!src)
    {
        ERR("PowerShell Core source directory not found\n");
        return FALSE;
    }

    if (progress) progress(30, 100, user_context);

    ExpandEnvironmentStringsW(L"%ProgramFiles%", dst, MAX_PATH);
    CreateDirectoryW(dst, NULL);
    lstrcatW(dst, L"\\PowerShell");
    CreateDirectoryW(dst, NULL);
    lstrcatW(dst, L"\\7");
    CreateDirectoryW(dst, NULL);

    if (!copy_or_hardlink_tree(src, dst))
    {
        ERR("Failed to copy PowerShell Core files from %s to %s\n", wine_dbgstr_w(src), wine_dbgstr_w(dst));
        return FALSE;
    }

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    if (progress) progress(70, 100, user_context);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\PowerShell\\3\\PowerShellEngine",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hkey, L"ApplicationBase", 0, REG_SZ,
                       (const BYTE *)dst, (lstrlenW(dst) + 1) * sizeof(WCHAR));
        RegCloseKey(hkey);
    }

    append_to_user_path(dst);
    return TRUE;
}

static BOOL disable_pwsh(HANDLE cancel_event, DISM_PROGRESS_CALLBACK progress, PVOID user_context)
{
    WCHAR dst[MAX_PATH];

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    if (progress) progress(30, 100, user_context);

    ExpandEnvironmentStringsW(L"%ProgramFiles%\\PowerShell\\7", dst, MAX_PATH);
    remove_directory_tree(dst);

    if (progress) progress(70, 100, user_context);

    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\3\\PowerShellEngine");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\3");

    remove_from_user_path(dst);
    return TRUE;
}

/* ========================================================================= */
/* Wine Mono (.NET 4.x) feature implementation                              */
/* ========================================================================= */

static BOOL is_mono_enabled(void)
{
    WCHAR path[MAX_PATH];
    HKEY hkey;
    REGSAM sam_flags[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    int s;

    ExpandEnvironmentStringsW(L"%SystemRoot%\\mono", path, MAX_PATH);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return TRUE;

    for (s = 0; s < 2; s++)
    {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full",
                          0, KEY_READ | sam_flags[s], &hkey) == ERROR_SUCCESS)
        {
            DWORD install = 0, size = sizeof(install);
            if (RegQueryValueExW(hkey, L"Install", NULL, NULL, (BYTE *)&install, &size) == ERROR_SUCCESS)
            {
                RegCloseKey(hkey);
                if (install == 1) return TRUE;
            }
            else
            {
                RegCloseKey(hkey);
                return TRUE;
            }
        }

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\.NETFramework",
                          0, KEY_READ | sam_flags[s], &hkey) == ERROR_SUCCESS)
        {
            DWORD install = 0, size = sizeof(install);
            if (RegQueryValueExW(hkey, L"Install", NULL, NULL, (BYTE *)&install, &size) == ERROR_SUCCESS)
            {
                RegCloseKey(hkey);
                if (install == 1) return TRUE;
            }
            else
            {
                RegCloseKey(hkey);
                return TRUE;
            }
        }
    }

    return FALSE;
}

static BOOL write_dotnet4_registry_keys(void)
{
    HKEY hkey;
    DWORD install_val = 1;
    DWORD release_val = 528040;
    const WCHAR version_str[] = L"4.8.0";
    const WCHAR target_version_str[] = L"4.0.0";
    WCHAR windir[MAX_PATH];
    WCHAR install_root[MAX_PATH];
    REGSAM sam_flags[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    int s;

    GetWindowsDirectoryW(windir, MAX_PATH);
    swprintf(install_root, MAX_PATH, L"%s\\Microsoft.NET\\Framework64\\", windir);

    for (s = 0; s < 2; s++)
    {
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Microsoft\\.NETFramework",
                            0, NULL, 0, KEY_SET_VALUE | sam_flags[s], NULL, &hkey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hkey, L"Install", 0, REG_DWORD, (const BYTE *)&install_val, sizeof(install_val));
            RegSetValueExW(hkey, L"Version", 0, REG_SZ, (const BYTE *)version_str, sizeof(version_str));
            RegSetValueExW(hkey, L"InstallRoot", 0, REG_SZ, (const BYTE *)install_root, (lstrlenW(install_root) + 1) * sizeof(WCHAR));
            RegCloseKey(hkey);
        }

        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full",
                            0, NULL, 0, KEY_SET_VALUE | sam_flags[s], NULL, &hkey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hkey, L"Install", 0, REG_DWORD, (const BYTE *)&install_val, sizeof(install_val));
            RegSetValueExW(hkey, L"Version", 0, REG_SZ, (const BYTE *)version_str, sizeof(version_str));
            RegSetValueExW(hkey, L"TargetVersion", 0, REG_SZ, (const BYTE *)target_version_str, sizeof(target_version_str));
            RegSetValueExW(hkey, L"Release", 0, REG_DWORD, (const BYTE *)&release_val, sizeof(release_val));
            RegCloseKey(hkey);
        }

        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Client",
                            0, NULL, 0, KEY_SET_VALUE | sam_flags[s], NULL, &hkey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hkey, L"Install", 0, REG_DWORD, (const BYTE *)&install_val, sizeof(install_val));
            RegSetValueExW(hkey, L"Version", 0, REG_SZ, (const BYTE *)version_str, sizeof(version_str));
            RegSetValueExW(hkey, L"TargetVersion", 0, REG_SZ, (const BYTE *)target_version_str, sizeof(target_version_str));
            RegSetValueExW(hkey, L"Release", 0, REG_DWORD, (const BYTE *)&release_val, sizeof(release_val));
            RegCloseKey(hkey);
        }
    }
    return TRUE;
}

static void clean_dotnet4_registry_keys(void)
{
    REGSAM sam_flags[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    int s;

    for (s = 0; s < 2; s++)
    {
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full",
                        sam_flags[s], 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Client",
                        sam_flags[s], 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4",
                        sam_flags[s], 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\.NETFramework",
                        sam_flags[s], 0);
    }
}

static BOOL search_dir_for_msi(const WCHAR *dir, WCHAR *msi_path, DWORD max_len)
{
    WIN32_FIND_DATAW fd;
    WCHAR search_path[MAX_PATH];
    HANDLE hFind;

    if (!dir || !*dir) return FALSE;
    if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) return FALSE;

    /* 1. Look for *mono*.msi */
    swprintf(search_path, MAX_PATH, L"%s\\*mono*.msi", dir);
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        swprintf(msi_path, max_len, L"%s\\%s", dir, fd.cFileName);
        FindClose(hFind);
        return TRUE;
    }

    /* 2. Look for any *.msi if directory name contains mono */
    if (wcsstr(dir, L"mono") || wcsstr(dir, L"Mono"))
    {
        swprintf(search_path, MAX_PATH, L"%s\\*.msi", dir);
        hFind = FindFirstFileW(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            swprintf(msi_path, max_len, L"%s\\%s", dir, fd.cFileName);
            FindClose(hFind);
            return TRUE;
        }
    }

    /* 3. Check immediate subdirectories */
    swprintf(search_path, MAX_PATH, L"%s\\*", dir);
    hFind = FindFirstFileW(search_path, &fd);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L".."))
            {
                WCHAR subdir[MAX_PATH];
                WCHAR support_dir[MAX_PATH];
                WIN32_FIND_DATAW sub_fd;
                HANDLE sub_find;

                swprintf(subdir, MAX_PATH, L"%s\\%s", dir, fd.cFileName);

                swprintf(search_path, MAX_PATH, L"%s\\*mono*.msi", subdir);
                sub_find = FindFirstFileW(search_path, &sub_fd);
                if (sub_find != INVALID_HANDLE_VALUE)
                {
                    swprintf(msi_path, max_len, L"%s\\%s", subdir, sub_fd.cFileName);
                    FindClose(sub_find);
                    FindClose(hFind);
                    return TRUE;
                }

                swprintf(support_dir, MAX_PATH, L"%s\\support", subdir);
                if (GetFileAttributesW(support_dir) != INVALID_FILE_ATTRIBUTES)
                {
                    swprintf(search_path, MAX_PATH, L"%s\\*mono*.msi", support_dir);
                    sub_find = FindFirstFileW(search_path, &sub_fd);
                    if (sub_find != INVALID_HANDLE_VALUE)
                    {
                        swprintf(msi_path, max_len, L"%s\\%s", support_dir, sub_fd.cFileName);
                        FindClose(sub_find);
                        FindClose(hFind);
                        return TRUE;
                    }
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    return FALSE;
}

static BOOL find_mono_msi(WCHAR *msi_path, DWORD max_len)
{
    static const WCHAR *standard_dirs[] = {
        L"Z:\\usr\\share\\wine\\mono",
        L"\\\\?\\unix\\usr\\share\\wine\\mono",
        L"Z:\\usr\\local\\share\\wine\\mono",
        L"\\\\?\\unix\\usr\\local\\share\\wine\\mono",
        L"Z:\\opt\\wine\\mono",
        L"\\\\?\\unix\\opt\\wine\\mono",
    };
    WCHAR candidate[MAX_PATH];
    WCHAR env_buf[MAX_PATH];
    HKEY hkey;
    int i;

    for (i = 0; i < ARRAY_SIZE(standard_dirs); i++)
    {
        if (search_dir_for_msi(standard_dirs[i], msi_path, max_len))
            return TRUE;
    }

    if (GetEnvironmentVariableW(L"WINEHOMEDIR", env_buf, MAX_PATH) && env_buf[0])
    {
        WCHAR *h = env_buf;
        if (!wcsncmp(h, L"\\??\\", 4)) h += 4;

        swprintf(candidate, MAX_PATH, L"%s\\.cache\\wine", h);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;

        swprintf(candidate, MAX_PATH, L"%s\\.cache\\wine\\mono", h);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;

        swprintf(candidate, MAX_PATH, L"%s\\mono", h);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;
    }

    if (GetEnvironmentVariableW(L"USERNAME", env_buf, MAX_PATH) && env_buf[0])
    {
        swprintf(candidate, MAX_PATH, L"Z:\\home\\%s\\.cache\\wine", env_buf);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;

        swprintf(candidate, MAX_PATH, L"Z:\\home\\%s\\.cache\\wine\\mono", env_buf);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;

        swprintf(candidate, MAX_PATH, L"Z:\\home\\%s\\mono", env_buf);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;
    }

    if (GetEnvironmentVariableW(L"HOME", env_buf, MAX_PATH) && env_buf[0])
    {
        swprintf(candidate, MAX_PATH, L"Z:%s\\.cache\\wine", env_buf);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;

        swprintf(candidate, MAX_PATH, L"Z:%s\\.cache\\wine\\mono", env_buf);
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;
    }

    if (ExpandEnvironmentStringsW(L"%SystemRoot%\\mono", candidate, MAX_PATH))
    {
        if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;
    }

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Wine\\Dotnet", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        DWORD type, size = sizeof(candidate);
        if (RegQueryValueExW(hkey, L"MonoCabDir", NULL, &type, (BYTE *)candidate, &size) == ERROR_SUCCESS &&
            type == REG_SZ)
        {
            RegCloseKey(hkey);
            if (search_dir_for_msi(candidate, msi_path, max_len)) return TRUE;
        }
        else
        {
            RegCloseKey(hkey);
        }
    }

    return FALSE;
}

static int execute_msiexec(const WCHAR *cmdline, HANDLE cancel_event)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD exit_code = 1;
    WCHAR cmd[MAX_PATH * 2 + 64];

    lstrcpynW(cmd, cmdline, ARRAY_SIZE(cmd));

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    TRACE("Executing: %s\n", wine_dbgstr_w(cmd));

    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        ERR("Failed to launch msiexec.exe\n");
        return 1;
    }

    if (cancel_event)
    {
        HANDLE handles[2] = { pi.hProcess, cancel_event };
        DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0 + 1)
        {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return ERROR_CANCELLED;
        }
    }
    else
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return (int)exit_code;
}

static BOOL enable_mono(HANDLE cancel_event, DISM_PROGRESS_CALLBACK progress, PVOID user_context)
{
    WCHAR msi[MAX_PATH];
    WCHAR cmd[MAX_PATH * 2 + 64];
    int ret;

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    if (!find_mono_msi(msi, ARRAY_SIZE(msi)))
    {
        ERR("Wine Mono MSI could not be found\n");
        return FALSE;
    }

    if (progress) progress(25, 100, user_context);

    swprintf(cmd, ARRAY_SIZE(cmd), L"msiexec.exe /i \"%s\" /qn", msi);
    ret = execute_msiexec(cmd, cancel_event);
    if (ret != 0)
    {
        ERR("msiexec install failed with code %d\n", ret);
        return FALSE;
    }

    if (progress) progress(75, 100, user_context);

    write_dotnet4_registry_keys();
    return TRUE;
}

static BOOL disable_mono(HANDLE cancel_event, DISM_PROGRESS_CALLBACK progress, PVOID user_context)
{
    WCHAR msi[MAX_PATH];
    HKEY hkey_uninst;
    WCHAR mono_dir[MAX_PATH];

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    if (progress) progress(20, 100, user_context);

    if (find_mono_msi(msi, ARRAY_SIZE(msi)))
    {
        WCHAR cmd[MAX_PATH * 2 + 64];
        swprintf(cmd, ARRAY_SIZE(cmd), L"msiexec.exe /x \"%s\" /qn", msi);
        execute_msiexec(cmd, cancel_event);
    }

    if (progress) progress(50, 100, user_context);

    /* Check uninstall registry for installed mono GUIDs */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                      0, KEY_READ, &hkey_uninst) == ERROR_SUCCESS)
    {
        DWORD idx = 0;
        WCHAR subkey_name[256];
        DWORD name_len = ARRAY_SIZE(subkey_name);

        while (RegEnumKeyExW(hkey_uninst, idx++, subkey_name, &name_len, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            HKEY hsub;
            if (RegOpenKeyExW(hkey_uninst, subkey_name, 0, KEY_READ, &hsub) == ERROR_SUCCESS)
            {
                WCHAR disp[256] = {0};
                DWORD disp_size = sizeof(disp);
                if (RegQueryValueExW(hsub, L"DisplayName", NULL, NULL, (BYTE *)disp, &disp_size) == ERROR_SUCCESS)
                {
                    if (wcsstr(disp, L"Wine Mono") || wcsstr(disp, L"wine mono"))
                    {
                        WCHAR cmd[512];
                        swprintf(cmd, ARRAY_SIZE(cmd), L"msiexec.exe /x %s /qn", subkey_name);
                        execute_msiexec(cmd, cancel_event);
                    }
                }
                RegCloseKey(hsub);
            }
            name_len = ARRAY_SIZE(subkey_name);
        }
        RegCloseKey(hkey_uninst);
    }

    if (progress) progress(80, 100, user_context);

    clean_dotnet4_registry_keys();

    if (ExpandEnvironmentStringsW(L"%SystemRoot%\\mono", mono_dir, MAX_PATH))
    {
        remove_directory_tree(mono_dir);
    }

    return TRUE;
}

/* ========================================================================= */
/* AppX Deployment Service feature implementation                            */
/* ========================================================================= */

static BOOL is_appx_enabled(void)
{
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"System\\CurrentControlSet\\Services\\AppXSvc",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        DWORD start = 0, size = sizeof(start);
        if (RegQueryValueExW(hkey, L"Start", NULL, NULL, (BYTE *)&start, &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hkey);
            return start != 4;
        }
        RegCloseKey(hkey);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        RegCloseKey(hkey);
        return TRUE;
    }
    return FALSE;
}

static BOOL enable_appx(HANDLE cancel_event, DISM_PROGRESS_CALLBACK progress, PVOID user_context)
{
    HKEY hkey;
    DWORD start_val = 3;
    DWORD type_val = 0x20;
    const WCHAR svc_name[] = L"AppX Deployment Service (AppXSvc)";

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    if (progress) progress(25, 100, user_context);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Packages",
                        0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
        RegCloseKey(hkey);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Exes",
                        0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
        RegCloseKey(hkey);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppModel\\Identity",
                        0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
        RegCloseKey(hkey);

    if (progress) progress(70, 100, user_context);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"System\\CurrentControlSet\\Services\\AppXSvc",
                        0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hkey, L"Start", 0, REG_DWORD, (const BYTE *)&start_val, sizeof(start_val));
        RegSetValueExW(hkey, L"Type", 0, REG_DWORD, (const BYTE *)&type_val, sizeof(type_val));
        RegSetValueExW(hkey, L"DisplayName", 0, REG_SZ, (const BYTE *)svc_name, sizeof(svc_name));
        RegCloseKey(hkey);
    }

    return TRUE;
}

static BOOL disable_appx(HANDLE cancel_event, DISM_PROGRESS_CALLBACK progress, PVOID user_context)
{
    HKEY hkey;
    DWORD start_val = 4; /* Disabled */

    if (cancel_event && WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0)
        return FALSE;

    if (progress) progress(50, 100, user_context);

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                        L"System\\CurrentControlSet\\Services\\AppXSvc",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hkey, L"Start", 0, REG_DWORD, (const BYTE *)&start_val, sizeof(start_val));
        RegCloseKey(hkey);
    }
    return TRUE;
}

/* ========================================================================= */
/* Public DISM API Exports                                                   */
/* ========================================================================= */

HRESULT WINAPI DismInitialize(DismLogLevel LogLevel, PCWSTR LogFilePath, PCWSTR ScratchDirectory)
{
    TRACE("(%d, %s, %s)\n", LogLevel, wine_dbgstr_w(LogFilePath), wine_dbgstr_w(ScratchDirectory));

    EnterCriticalSection(&g_cs);
    if (g_init_count > 0)
    {
        LeaveCriticalSection(&g_cs);
        return DISMAPI_E_DISMAPI_ALREADY_INITIALIZED;
    }

    g_init_count = 1;
    g_log_level = LogLevel;
    if (LogFilePath)
        lstrcpynW(g_log_path, LogFilePath, ARRAY_SIZE(g_log_path));
    else
        g_log_path[0] = 0;

    if (ScratchDirectory)
        lstrcpynW(g_scratch_dir, ScratchDirectory, ARRAY_SIZE(g_scratch_dir));
    else
        g_scratch_dir[0] = 0;

    LeaveCriticalSection(&g_cs);
    return S_OK;
}

HRESULT WINAPI DismShutdown(void)
{
    UINT i;
    TRACE("()\n");

    EnterCriticalSection(&g_cs);
    if (g_init_count > 0)
    {
        g_init_count = 0;
        for (i = 0; i < MAX_SESSIONS; i++)
            g_sessions[i].in_use = FALSE;
    }
    LeaveCriticalSection(&g_cs);
    return S_OK;
}

HRESULT WINAPI DismOpenSession(PCWSTR ImagePath, PCWSTR WindowsDirectory, PCWSTR SystemDrive, DismSession *Session)
{
    UINT i;
    TRACE("(%s, %s, %s, %p)\n", wine_dbgstr_w(ImagePath), wine_dbgstr_w(WindowsDirectory), wine_dbgstr_w(SystemDrive), Session);

    if (!Session || !ImagePath)
        return E_INVALIDARG;

    EnterCriticalSection(&g_cs);
    for (i = 0; i < MAX_SESSIONS; i++)
    {
        if (!g_sessions[i].in_use)
        {
            g_sessions[i].in_use = TRUE;
            lstrcpynW(g_sessions[i].image_path, ImagePath, ARRAY_SIZE(g_sessions[i].image_path));
            *Session = (DismSession)(i + 1);
            LeaveCriticalSection(&g_cs);
            return S_OK;
        }
    }
    LeaveCriticalSection(&g_cs);

    return HRESULT_FROM_WIN32(ERROR_OUT_OF_STRUCTURES);
}

HRESULT WINAPI DismCloseSession(DismSession Session)
{
    TRACE("(%u)\n", Session);

    if (Session == 0 || Session > MAX_SESSIONS)
        return DISMAPI_E_INVALID_DISM_SESSION;

    EnterCriticalSection(&g_cs);
    if (!g_sessions[Session - 1].in_use)
    {
        LeaveCriticalSection(&g_cs);
        return DISMAPI_E_INVALID_DISM_SESSION;
    }
    g_sessions[Session - 1].in_use = FALSE;
    LeaveCriticalSection(&g_cs);

    return S_OK;
}

HRESULT WINAPI DismGetFeatures(DismSession Session, PCWSTR Identifier, DismPackageType PackageType, DismFeature **Feature, UINT *Count)
{
    DismFeature *features;
    size_t total_size;
    WCHAR *str_ptr;

    TRACE("(%u, %s, %d, %p, %p)\n", Session, wine_dbgstr_w(Identifier), PackageType, Feature, Count);

    if (!Feature || !Count)
        return E_INVALIDARG;

    if (!is_valid_session(Session))
        return DISMAPI_E_INVALID_DISM_SESSION;

    total_size = 3 * sizeof(DismFeature) +
                 (wcslen(FEATURE_PWSH) + 1) * sizeof(WCHAR) +
                 (wcslen(FEATURE_MONO) + 1) * sizeof(WCHAR) +
                 (wcslen(FEATURE_APPX) + 1) * sizeof(WCHAR);

    features = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total_size);
    if (!features)
        return E_OUTOFMEMORY;

    str_ptr = (WCHAR *)(features + 3);

    features[0].FeatureName = str_ptr;
    wcscpy(str_ptr, FEATURE_PWSH);
    str_ptr += wcslen(str_ptr) + 1;
    features[0].State = is_pwsh_enabled() ? DismStateInstalled : DismStateNotPresent;

    features[1].FeatureName = str_ptr;
    wcscpy(str_ptr, FEATURE_MONO);
    str_ptr += wcslen(str_ptr) + 1;
    features[1].State = is_mono_enabled() ? DismStateInstalled : DismStateNotPresent;

    features[2].FeatureName = str_ptr;
    wcscpy(str_ptr, FEATURE_APPX);
    features[2].State = is_appx_enabled() ? DismStateInstalled : DismStateNotPresent;

    *Feature = features;
    *Count = 3;
    return S_OK;
}

HRESULT WINAPI DismGetFeatureInfo(DismSession Session, PCWSTR FeatureName, PCWSTR Identifier, DismPackageType PackageType, DismFeatureInfo **FeatureInfo)
{
    const WCHAR *canonical_name;
    const WCHAR *display_name;
    const WCHAR *description;
    DismPackageFeatureState state;
    DismFeatureInfo *info;
    size_t total_size;
    WCHAR *str_ptr;

    TRACE("(%u, %s, %s, %d, %p)\n", Session, wine_dbgstr_w(FeatureName), wine_dbgstr_w(Identifier), PackageType, FeatureInfo);

    if (!FeatureName || !FeatureInfo)
        return E_INVALIDARG;

    if (!is_valid_session(Session))
        return DISMAPI_E_INVALID_DISM_SESSION;

    if (!wcsicmp(FeatureName, FEATURE_PWSH))
    {
        canonical_name = FEATURE_PWSH;
        display_name = L"Microsoft PowerShell Core";
        description = L"PowerShell Core 7 cross-platform automation and configuration tool";
        state = is_pwsh_enabled() ? DismStateInstalled : DismStateNotPresent;
    }
    else if (!wcsicmp(FeatureName, FEATURE_MONO) || !wcsicmp(FeatureName, FEATURE_NETFX4))
    {
        canonical_name = FEATURE_MONO;
        display_name = L"Wine Mono .NET Runtime";
        description = L"Wine Mono .NET Framework 4.x runtime and support assemblies";
        state = is_mono_enabled() ? DismStateInstalled : DismStateNotPresent;
    }
    else if (!wcsicmp(FeatureName, FEATURE_APPX) || !wcsicmp(FeatureName, FEATURE_APPXSVC))
    {
        canonical_name = FEATURE_APPX;
        display_name = L"AppX Deployment Service (AppXSvc)";
        description = L"Provides infrastructure support for deploying Store and UWP applications";
        state = is_appx_enabled() ? DismStateInstalled : DismStateNotPresent;
    }
    else
    {
        return DISMAPI_E_UNKNOWN_FEATURE;
    }

    total_size = sizeof(DismFeatureInfo) +
                 (wcslen(canonical_name) + 1) * sizeof(WCHAR) +
                 (wcslen(display_name) + 1) * sizeof(WCHAR) +
                 (wcslen(description) + 1) * sizeof(WCHAR);

    info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total_size);
    if (!info)
        return E_OUTOFMEMORY;

    str_ptr = (WCHAR *)(info + 1);

    info->FeatureName = str_ptr;
    wcscpy(str_ptr, canonical_name);
    str_ptr += wcslen(str_ptr) + 1;

    info->DisplayName = str_ptr;
    wcscpy(str_ptr, display_name);
    str_ptr += wcslen(str_ptr) + 1;

    info->Description = str_ptr;
    wcscpy(str_ptr, description);

    info->FeatureState = state;
    info->RestartRequired = DismRestartNo;
    info->CustomProperty = NULL;
    info->CustomPropertyCount = 0;

    *FeatureInfo = info;
    return S_OK;
}

HRESULT WINAPI DismEnableFeature(DismSession Session, PCWSTR FeatureName, PCWSTR Identifier, DismPackageType PackageType, BOOL LimitAccess, PCWSTR *SourcePaths, UINT SourcePathCount, BOOL EnableAllDependencies, HANDLE CancelEvent, DISM_PROGRESS_CALLBACK Progress, PVOID UserContext)
{
    HRESULT hr = S_OK;

    TRACE("(%u, %s, ...)\n", Session, wine_dbgstr_w(FeatureName));

    if (!FeatureName)
        return E_INVALIDARG;

    if (!is_valid_session(Session))
        return DISMAPI_E_INVALID_DISM_SESSION;

    if (CancelEvent && WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    if (Progress) Progress(10, 100, UserContext);

    if (!wcsicmp(FeatureName, FEATURE_PWSH))
    {
        if (!enable_pwsh(SourcePaths, SourcePathCount, CancelEvent, Progress, UserContext))
            hr = E_FAIL;
    }
    else if (!wcsicmp(FeatureName, FEATURE_MONO) || !wcsicmp(FeatureName, FEATURE_NETFX4))
    {
        if (!enable_mono(CancelEvent, Progress, UserContext))
            hr = E_FAIL;
    }
    else if (!wcsicmp(FeatureName, FEATURE_APPX) || !wcsicmp(FeatureName, FEATURE_APPXSVC))
    {
        if (!enable_appx(CancelEvent, Progress, UserContext))
            hr = E_FAIL;
    }
    else
    {
        return DISMAPI_E_UNKNOWN_FEATURE;
    }

    if (SUCCEEDED(hr) && Progress)
        Progress(100, 100, UserContext);

    return hr;
}

HRESULT WINAPI DismDisableFeature(DismSession Session, PCWSTR FeatureName, PCWSTR PackageName, BOOL RemovePayload, HANDLE CancelEvent, DISM_PROGRESS_CALLBACK Progress, PVOID UserContext)
{
    HRESULT hr = S_OK;

    TRACE("(%u, %s, ...)\n", Session, wine_dbgstr_w(FeatureName));

    if (!FeatureName)
        return E_INVALIDARG;

    if (!is_valid_session(Session))
        return DISMAPI_E_INVALID_DISM_SESSION;

    if (CancelEvent && WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    if (Progress) Progress(10, 100, UserContext);

    if (!wcsicmp(FeatureName, FEATURE_PWSH))
    {
        if (!disable_pwsh(CancelEvent, Progress, UserContext))
            hr = E_FAIL;
    }
    else if (!wcsicmp(FeatureName, FEATURE_MONO) || !wcsicmp(FeatureName, FEATURE_NETFX4))
    {
        if (!disable_mono(CancelEvent, Progress, UserContext))
            hr = E_FAIL;
    }
    else if (!wcsicmp(FeatureName, FEATURE_APPX) || !wcsicmp(FeatureName, FEATURE_APPXSVC))
    {
        if (!disable_appx(CancelEvent, Progress, UserContext))
            hr = E_FAIL;
    }
    else
    {
        return DISMAPI_E_UNKNOWN_FEATURE;
    }

    if (SUCCEEDED(hr) && Progress)
        Progress(100, 100, UserContext);

    return hr;
}

HRESULT WINAPI DismDelete(VOID *DismStructure)
{
    TRACE("(%p)\n", DismStructure);
    if (DismStructure)
        HeapFree(GetProcessHeap(), 0, DismStructure);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, void *reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinst);
        break;
    }
    return TRUE;
}
