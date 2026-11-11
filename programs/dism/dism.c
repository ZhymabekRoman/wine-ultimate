/*
 * Copyright 2018 Austin English
 * Copyright 2026 Zhymabek Roman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the License Foundation; either
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

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dism);

#define FEATURE_PWSH L"Microsoft-PowerShell-Core"
#define FEATURE_MONO L"NetFx4-WineMono"

static void output_write(const WCHAR *str)
{
    DWORD count, len = lstrlenW(str);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!WriteConsoleW(out, str, len, &count, NULL))
    {
        DWORD alen = WideCharToMultiByte(CP_OEMCP, 0, str, len, NULL, 0, NULL, NULL);
        char *msgA = malloc(alen);
        if (msgA)
        {
            WideCharToMultiByte(CP_OEMCP, 0, str, len, msgA, alen, NULL, NULL);
            WriteFile(out, msgA, alen, &count, FALSE);
            free(msgA);
        }
    }
}

static void WINAPIV output_printf(const WCHAR *fmt, ...)
{
    WCHAR buf[2048];
    va_list va;
    va_start(va, fmt);
    vswprintf(buf, ARRAY_SIZE(buf), fmt, va);
    va_end(va);
    output_write(buf);
}

static void print_header(void)
{
    output_write(L"\r\nDeployment Image Servicing and Management tool\r\n"
                 L"Version: 10.0.18362.900\r\n\r\n"
                 L"Image Version: 10.0.18362.900\r\n\r\n");
}

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

static BOOL is_mono_enabled(void)
{
    WCHAR path[MAX_PATH];
    HKEY hkey;

    ExpandEnvironmentStringsW(L"%SystemRoot%\\mono", path, MAX_PATH);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return TRUE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        RegCloseKey(hkey);
        return TRUE;
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

static BOOL find_mono_msi(WCHAR *msi_path, DWORD max_len)
{
    static const WCHAR *candidates[] = {
        L"Z:\\usr\\share\\wine\\mono",
        L"Z:\\usr\\share\\wine\\mono\\wine-mono-11.3.0\\support",
        L"Z:\\usr\\share\\wine\\mono\\wine-mono-11.1.0\\support",
        L"Z:\\usr\\share\\wine\\mono\\wine-mono-10.4.0\\support",
    };
    WCHAR search_path[MAX_PATH];
    WCHAR home_cache[MAX_PATH];
    WCHAR username[64];
    DWORD ulen = ARRAY_SIZE(username);
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    int i;

    /* Check static candidates */
    for (i = 0; i < ARRAY_SIZE(candidates); i++)
    {
        swprintf(search_path, MAX_PATH, L"%s\\*mono*.msi", candidates[i]);
        hFind = FindFirstFileW(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            swprintf(msi_path, max_len, L"%s\\%s", candidates[i], fd.cFileName);
            FindClose(hFind);
            return TRUE;
        }
    }

    /* Check ~/.cache/wine/mono via username */
    if (GetEnvironmentVariableW(L"USERNAME", username, ulen) && username[0])
    {
        swprintf(home_cache, MAX_PATH, L"Z:\\home\\%s\\.cache\\wine\\mono", username);
        swprintf(search_path, MAX_PATH, L"%s\\*mono*.msi", home_cache);
        hFind = FindFirstFileW(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            swprintf(msi_path, max_len, L"%s\\%s", home_cache, fd.cFileName);
            FindClose(hFind);
            return TRUE;
        }

        /* Check ~/mono */
        swprintf(home_cache, MAX_PATH, L"Z:\\home\\%s\\mono", username);
        swprintf(search_path, MAX_PATH, L"%s\\*mono*.msi", home_cache);
        hFind = FindFirstFileW(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            swprintf(msi_path, max_len, L"%s\\%s", home_cache, fd.cFileName);
            FindClose(hFind);
            return TRUE;
        }
    }

    /* Check WINEHOMEDIR */
    if (GetEnvironmentVariableW(L"WINEHOMEDIR", home_cache, MAX_PATH))
    {
        WCHAR *h = home_cache;
        if (!wcsncmp(h, L"\\??\\", 4)) h += 4;
        swprintf(search_path, MAX_PATH, L"%s\\.cache\\wine\\mono\\*mono*.msi", h);
        hFind = FindFirstFileW(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            swprintf(msi_path, max_len, L"%s\\.cache\\wine\\mono\\%s", h, fd.cFileName);
            FindClose(hFind);
            return TRUE;
        }

        swprintf(search_path, MAX_PATH, L"%s\\mono\\*mono*.msi", h);
        hFind = FindFirstFileW(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            swprintf(msi_path, max_len, L"%s\\mono\\%s", h, fd.cFileName);
            FindClose(hFind);
            return TRUE;
        }
    }

    return FALSE;
}

static int do_get_features(void)
{
    print_header();
    output_write(L"Features listing for image :\r\n\r\n"
                 L"------------------------------------------- | --------\r\n"
                 L"Feature Name                                | State\r\n"
                 L"------------------------------------------- | --------\r\n");

    output_printf(L"%-43s | %s\r\n", FEATURE_PWSH, is_pwsh_enabled() ? L"Enabled" : L"Disabled");
    output_printf(L"%-43s | %s\r\n", FEATURE_MONO, is_mono_enabled() ? L"Enabled" : L"Disabled");

    output_write(L"\r\nThe operation completed successfully.\r\n");
    return 0;
}

static int do_get_feature_info(const WCHAR *feature_name)
{
    if (!feature_name || !*feature_name)
    {
        output_write(L"Error: A feature name must be specified.\r\n");
        return 1;
    }

    print_header();

    if (!wcsicmp(feature_name, FEATURE_PWSH))
    {
        output_printf(L"Feature Information:\r\n\r\n"
                      L"Feature Name : %s\r\n"
                      L"Display Name : Microsoft PowerShell Core\r\n"
                      L"Description : PowerShell Core 7 cross-platform automation and configuration tool\r\n"
                      L"Restart Required : No\r\n"
                      L"State : %s\r\n\r\n"
                      L"The operation completed successfully.\r\n",
                      FEATURE_PWSH, is_pwsh_enabled() ? L"Enabled" : L"Disabled");
        return 0;
    }
    else if (!wcsicmp(feature_name, FEATURE_MONO))
    {
        output_printf(L"Feature Information:\r\n\r\n"
                      L"Feature Name : %s\r\n"
                      L"Display Name : Wine Mono .NET Runtime\r\n"
                      L"Description : Wine Mono .NET Framework 4.x runtime and support assemblies\r\n"
                      L"Restart Required : No\r\n"
                      L"State : %s\r\n\r\n"
                      L"The operation completed successfully.\r\n",
                      FEATURE_MONO, is_mono_enabled() ? L"Enabled" : L"Disabled");
        return 0;
    }

    output_printf(L"Feature name %s is unknown.\r\n"
                  L"A Windows feature name was not recognized.\r\n",
                  feature_name);
    return 1;
}

static int do_enable_feature(const WCHAR *feature_name)
{
    if (!feature_name || !*feature_name)
    {
        output_write(L"Error: A feature name must be specified.\r\n");
        return 1;
    }

    print_header();

    if (!wcsicmp(feature_name, FEATURE_PWSH))
    {
        WCHAR dst[MAX_PATH];
        WCHAR src[] = L"Z:\\var\\tmp\\pwsh-win-x64";
        HKEY hkey;

        output_write(L"Enabling feature(s)\r\n");

        ExpandEnvironmentStringsW(L"%ProgramFiles%", dst, MAX_PATH);
        CreateDirectoryW(dst, NULL);
        lstrcatW(dst, L"\\PowerShell");
        CreateDirectoryW(dst, NULL);
        lstrcatW(dst, L"\\7");
        CreateDirectoryW(dst, NULL);

        if (!copy_or_hardlink_tree(src, dst))
        {
            output_write(L"An error occurred while copying PowerShell Core files.\r\n");
            return 1;
        }

        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                            L"SOFTWARE\\Microsoft\\PowerShell\\3\\PowerShellEngine",
                            0, NULL, 0, KEY_SET_VALUE, NULL, &hkey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hkey, L"ApplicationBase", 0, REG_SZ,
                           (const BYTE *)dst, (lstrlenW(dst) + 1) * sizeof(WCHAR));
            RegCloseKey(hkey);
        }

        append_to_user_path(dst);

        output_write(L"[==========================100.0%==========================]\r\n"
                     L"The operation completed successfully.\r\n");
        return 0;
    }
    else if (!wcsicmp(feature_name, FEATURE_MONO))
    {
        WCHAR msi[MAX_PATH];
        WCHAR cmd[MAX_PATH * 2];
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        output_write(L"Enabling feature(s)\r\n");

        if (!find_mono_msi(msi, ARRAY_SIZE(msi)))
        {
            output_write(L"Error: wine-mono MSI could not be found.\r\n");
            return 1;
        }

        swprintf(cmd, ARRAY_SIZE(cmd), L"msiexec.exe /i \"%s\" /qn", msi);
        if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else
        {
            output_write(L"Error: Failed to launch msiexec.exe.\r\n");
            return 1;
        }

        output_write(L"[==========================100.0%==========================]\r\n"
                     L"The operation completed successfully.\r\n");
        return 0;
    }

    output_printf(L"Feature name %s is unknown.\r\n", feature_name);
    return 1;
}

static int do_disable_feature(const WCHAR *feature_name)
{
    if (!feature_name || !*feature_name)
    {
        output_write(L"Error: A feature name must be specified.\r\n");
        return 1;
    }

    print_header();

    if (!wcsicmp(feature_name, FEATURE_PWSH))
    {
        WCHAR dst[MAX_PATH];

        output_write(L"Disabling feature(s)\r\n");

        ExpandEnvironmentStringsW(L"%ProgramFiles%\\PowerShell\\7", dst, MAX_PATH);
        remove_directory_tree(dst);

        RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\3\\PowerShellEngine");
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\PowerShell\\3");

        remove_from_user_path(dst);

        output_write(L"[==========================100.0%==========================]\r\n"
                     L"The operation completed successfully.\r\n");
        return 0;
    }
    else if (!wcsicmp(feature_name, FEATURE_MONO))
    {
        WCHAR msi[MAX_PATH];
        HKEY hkey_uninst;

        output_write(L"Disabling feature(s)\r\n");

        if (find_mono_msi(msi, ARRAY_SIZE(msi)))
        {
            WCHAR cmd[MAX_PATH * 2];
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            swprintf(cmd, ARRAY_SIZE(cmd), L"msiexec.exe /x \"%s\" /qn", msi);
            if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }

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
                            STARTUPINFOW si = { sizeof(si) };
                            PROCESS_INFORMATION pi;
                            swprintf(cmd, ARRAY_SIZE(cmd), L"msiexec.exe /x %s /qn", subkey_name);
                            if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
                            {
                                WaitForSingleObject(pi.hProcess, INFINITE);
                                CloseHandle(pi.hThread);
                                CloseHandle(pi.hProcess);
                            }
                        }
                    }
                    RegCloseKey(hsub);
                }
                name_len = ARRAY_SIZE(subkey_name);
            }
            RegCloseKey(hkey_uninst);
        }

        output_write(L"[==========================100.0%==========================]\r\n"
                     L"The operation completed successfully.\r\n");
        return 0;
    }

    output_printf(L"Feature name %s is unknown.\r\n", feature_name);
    return 1;
}

static void show_help(void)
{
    print_header();
    output_write(L"Deployment Image Servicing and Management tool\r\n\r\n"
                 L"DISM.exe [dism_options] {servicing_command} [command_arguments]\r\n\r\n"
                 L"  /Online                         - Targets the running operating system.\r\n"
                 L"  /Get-Features                   - Displays information about all features.\r\n"
                 L"  /Get-FeatureInfo                - Displays detailed information about a specific feature.\r\n"
                 L"  /Enable-Feature                 - Enables a specific feature in the image.\r\n"
                 L"  /Disable-Feature                - Disables a specific feature in the image.\r\n\r\n"
                 L"The operation completed successfully.\r\n");
}

int __cdecl wmain(int argc, WCHAR *argv[])
{
    BOOL get_features = FALSE;
    BOOL get_feature_info = FALSE;
    BOOL enable_feature = FALSE;
    BOOL disable_feature = FALSE;
    const WCHAR *feature_name = NULL;
    int i;

    if (argc < 2)
    {
        show_help();
        return 0;
    }

    for (i = 1; i < argc; i++)
    {
        const WCHAR *arg = argv[i];

        if (!wcsicmp(arg, L"/?") || !wcsicmp(arg, L"-?") ||
            !wcsicmp(arg, L"/help") || !wcsicmp(arg, L"--help"))
        {
            show_help();
            return 0;
        }

        if (!wcsicmp(arg, L"/Online") || !wcsicmp(arg, L"-Online"))
        {
            continue;
        }

        if (!wcsicmp(arg, L"/Get-Features") || !wcsicmp(arg, L"-Get-Features"))
        {
            get_features = TRUE;
            continue;
        }

        if (!wcsicmp(arg, L"/Get-FeatureInfo") || !wcsicmp(arg, L"-Get-FeatureInfo"))
        {
            get_feature_info = TRUE;
            continue;
        }

        if (!wcsicmp(arg, L"/Enable-Feature") || !wcsicmp(arg, L"-Enable-Feature"))
        {
            enable_feature = TRUE;
            continue;
        }

        if (!wcsicmp(arg, L"/Disable-Feature") || !wcsicmp(arg, L"-Disable-Feature"))
        {
            disable_feature = TRUE;
            continue;
        }

        if (!_wcsnicmp(arg, L"/FeatureName:", 13) || !_wcsnicmp(arg, L"-FeatureName:", 13))
        {
            feature_name = arg + 13;
            continue;
        }

        if ((!wcsicmp(arg, L"/FeatureName") || !wcsicmp(arg, L"-FeatureName")) && i + 1 < argc)
        {
            feature_name = argv[++i];
            continue;
        }
    }

    if (get_features)
        return do_get_features();

    if (get_feature_info)
        return do_get_feature_info(feature_name);

    if (enable_feature)
        return do_enable_feature(feature_name);

    if (disable_feature)
        return do_disable_feature(feature_name);

    show_help();
    return 0;
}
