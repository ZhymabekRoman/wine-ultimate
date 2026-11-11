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

#include "dismapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dism);

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

static void CALLBACK dism_cli_progress(UINT current, UINT total, PVOID userData)
{
    if (total > 0 && current == total)
    {
        output_write(L"[==========================100.0%==========================]\r\n");
    }
}

static int do_get_features(void)
{
    DismSession session;
    DismFeature *features = NULL;
    UINT count = 0, i;
    HRESULT hr;

    print_header();

    hr = DismInitialize(DismLogErrorsWarningsInfo, NULL, NULL);
    if (FAILED(hr) && hr != DISMAPI_E_DISMAPI_ALREADY_INITIALIZED)
    {
        output_printf(L"Error initializing DISM API: 0x%08lx\r\n", hr);
        return 1;
    }

    hr = DismOpenSession(DISM_ONLINE_IMAGE, NULL, NULL, &session);
    if (FAILED(hr))
    {
        output_printf(L"Error opening DISM session: 0x%08lx\r\n", hr);
        DismShutdown();
        return 1;
    }

    output_write(L"Features listing for image :\r\n\r\n"
                 L"------------------------------------------- | --------\r\n"
                 L"Feature Name                                | State\r\n"
                 L"------------------------------------------- | --------\r\n");

    hr = DismGetFeatures(session, NULL, DismPackageNone, &features, &count);
    if (SUCCEEDED(hr) && features)
    {
        for (i = 0; i < count; i++)
        {
            output_printf(L"%-43s | %s\r\n",
                          features[i].FeatureName,
                          features[i].State == DismStateInstalled ? L"Enabled" : L"Disabled");
        }
        DismDelete(features);
    }

    DismCloseSession(session);
    DismShutdown();

    output_write(L"\r\nThe operation completed successfully.\r\n");
    return 0;
}

static int do_get_feature_info(const WCHAR *feature_name)
{
    DismSession session;
    DismFeatureInfo *info = NULL;
    HRESULT hr;

    if (!feature_name || !*feature_name)
    {
        output_write(L"Error: A feature name must be specified.\r\n");
        return 1;
    }

    print_header();

    hr = DismInitialize(DismLogErrorsWarningsInfo, NULL, NULL);
    if (FAILED(hr) && hr != DISMAPI_E_DISMAPI_ALREADY_INITIALIZED)
    {
        output_printf(L"Error initializing DISM API: 0x%08lx\r\n", hr);
        return 1;
    }

    hr = DismOpenSession(DISM_ONLINE_IMAGE, NULL, NULL, &session);
    if (FAILED(hr))
    {
        output_printf(L"Error opening DISM session: 0x%08lx\r\n", hr);
        DismShutdown();
        return 1;
    }

    hr = DismGetFeatureInfo(session, feature_name, NULL, DismPackageNone, &info);
    if (SUCCEEDED(hr) && info)
    {
        output_printf(L"Feature Information:\r\n\r\n"
                      L"Feature Name : %s\r\n"
                      L"Display Name : %s\r\n"
                      L"Description : %s\r\n"
                      L"Restart Required : %s\r\n"
                      L"State : %s\r\n\r\n"
                      L"The operation completed successfully.\r\n",
                      info->FeatureName,
                      info->DisplayName,
                      info->Description,
                      info->RestartRequired == DismRestartRequired ? L"Yes" : L"No",
                      info->FeatureState == DismStateInstalled ? L"Enabled" : L"Disabled");
        DismDelete(info);
        DismCloseSession(session);
        DismShutdown();
        return 0;
    }

    DismCloseSession(session);
    DismShutdown();

    output_printf(L"Feature name %s is unknown.\r\n"
                  L"A Windows feature name was not recognized.\r\n",
                  feature_name);
    return 1;
}

static int do_enable_feature(const WCHAR *feature_name)
{
    DismSession session;
    HRESULT hr;

    if (!feature_name || !*feature_name)
    {
        output_write(L"Error: A feature name must be specified.\r\n");
        return 1;
    }

    print_header();
    output_write(L"Enabling feature(s)\r\n");

    hr = DismInitialize(DismLogErrorsWarningsInfo, NULL, NULL);
    if (FAILED(hr) && hr != DISMAPI_E_DISMAPI_ALREADY_INITIALIZED)
    {
        output_printf(L"Error initializing DISM API: 0x%08lx\r\n", hr);
        return 1;
    }

    hr = DismOpenSession(DISM_ONLINE_IMAGE, NULL, NULL, &session);
    if (FAILED(hr))
    {
        output_printf(L"Error opening DISM session: 0x%08lx\r\n", hr);
        DismShutdown();
        return 1;
    }

    hr = DismEnableFeature(session, feature_name, NULL, DismPackageNone,
                           FALSE, NULL, 0, TRUE, NULL, dism_cli_progress, NULL);

    DismCloseSession(session);
    DismShutdown();

    if (SUCCEEDED(hr))
    {
        output_write(L"The operation completed successfully.\r\n");
        return 0;
    }

    if (hr == DISMAPI_E_UNKNOWN_FEATURE)
        output_printf(L"Feature name %s is unknown.\r\n", feature_name);
    else
        output_printf(L"Error enabling feature: 0x%08lx\r\n", hr);
    return 1;
}

static int do_disable_feature(const WCHAR *feature_name)
{
    DismSession session;
    HRESULT hr;

    if (!feature_name || !*feature_name)
    {
        output_write(L"Error: A feature name must be specified.\r\n");
        return 1;
    }

    print_header();
    output_write(L"Disabling feature(s)\r\n");

    hr = DismInitialize(DismLogErrorsWarningsInfo, NULL, NULL);
    if (FAILED(hr) && hr != DISMAPI_E_DISMAPI_ALREADY_INITIALIZED)
    {
        output_printf(L"Error initializing DISM API: 0x%08lx\r\n", hr);
        return 1;
    }

    hr = DismOpenSession(DISM_ONLINE_IMAGE, NULL, NULL, &session);
    if (FAILED(hr))
    {
        output_printf(L"Error opening DISM session: 0x%08lx\r\n", hr);
        DismShutdown();
        return 1;
    }

    hr = DismDisableFeature(session, feature_name, NULL, FALSE, NULL, dism_cli_progress, NULL);

    DismCloseSession(session);
    DismShutdown();

    if (SUCCEEDED(hr))
    {
        output_write(L"The operation completed successfully.\r\n");
        return 0;
    }

    if (hr == DISMAPI_E_UNKNOWN_FEATURE)
        output_printf(L"Feature name %s is unknown.\r\n", feature_name);
    else
        output_printf(L"Error disabling feature: 0x%08lx\r\n", hr);
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
