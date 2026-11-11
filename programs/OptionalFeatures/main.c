/*
 * Optional Features GUI application
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
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "dismapi.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(optfeatures);

struct feature_item {
    WCHAR name[64];
    WCHAR display_name[128];
    WCHAR description[256];
    DismPackageFeatureState initial_state;
};

struct dialog_data {
    DismSession session;
    struct feature_item *items;
    UINT item_count;
    HWND hDlg;
    HWND hList;
    HWND hProgress;
    HWND hStatus;
    HWND hDesc;
    HWND hOk;
    HWND hCancel;
};

static void set_listview_check_state(HWND hList, int index, BOOL checked)
{
    LVITEMW lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.stateMask = LVIS_STATEIMAGEMASK;
    lvi.state = INDEXTOSTATEIMAGEMASK(checked ? 2 : 1);
    SendMessageW(hList, LVM_SETITEMSTATE, index, (LPARAM)&lvi);
}

static BOOL get_listview_check_state(HWND hList, int index)
{
    UINT state = (UINT)SendMessageW(hList, LVM_GETITEMSTATE, index, LVIS_STATEIMAGEMASK);
    return ((state >> 12) - 1) != 0;
}

static void select_listview_item(HWND hList, int index)
{
    LVITEMW lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(hList, LVM_SETITEMSTATE, index, (LPARAM)&lvi);
}

static void CALLBACK progress_callback(UINT current, UINT total, PVOID user_data)
{
    struct dialog_data *data = (struct dialog_data *)user_data;
    MSG msg;

    if (data && data->hProgress && total > 0)
    {
        int pos = (int)((current * 100) / total);
        SendMessageW(data->hProgress, PBM_SETPOS, pos, 0);

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

static void populate_features(struct dialog_data *data)
{
    DismFeature *features = NULL;
    UINT count = 0, i;
    HRESULT hr;

    hr = DismGetFeatures(data->session, NULL, DismPackageNone, &features, &count);
    if (FAILED(hr) || !features)
    {
        WINE_ERR("DismGetFeatures failed: 0x%08lx\n", hr);
        return;
    }

    data->items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(struct feature_item));
    if (!data->items)
    {
        DismDelete(features);
        return;
    }
    data->item_count = count;

    for (i = 0; i < count; i++)
    {
        DismFeatureInfo *info = NULL;
        LVITEMW lvi;

        lstrcpynW(data->items[i].name, features[i].FeatureName, ARRAY_SIZE(data->items[i].name));
        data->items[i].initial_state = features[i].State;

        hr = DismGetFeatureInfo(data->session, features[i].FeatureName, NULL, DismPackageNone, &info);
        if (SUCCEEDED(hr) && info)
        {
            if (info->DisplayName && *info->DisplayName)
                lstrcpynW(data->items[i].display_name, info->DisplayName, ARRAY_SIZE(data->items[i].display_name));
            else
                lstrcpynW(data->items[i].display_name, features[i].FeatureName, ARRAY_SIZE(data->items[i].display_name));

            if (info->Description && *info->Description)
                lstrcpynW(data->items[i].description, info->Description, ARRAY_SIZE(data->items[i].description));

            DismDelete(info);
        }
        else
        {
            lstrcpynW(data->items[i].display_name, features[i].FeatureName, ARRAY_SIZE(data->items[i].display_name));
        }

        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = data->items[i].display_name;
        lvi.lParam = (LPARAM)i;

        (void)ListView_InsertItemW(data->hList, &lvi);
        set_listview_check_state(data->hList, i, data->items[i].initial_state == DismStateInstalled);
    }

    DismDelete(features);

    if (count > 0)
    {
        select_listview_item(data->hList, 0);
        SetWindowTextW(data->hDesc, data->items[0].description);
    }
}

static void apply_changes(struct dialog_data *data)
{
    UINT i;
    BOOL any_changed = FALSE;

    for (i = 0; i < data->item_count; i++)
    {
        BOOL checked = get_listview_check_state(data->hList, i);
        BOOL was_installed = (data->items[i].initial_state == DismStateInstalled);
        if (checked != was_installed)
        {
            any_changed = TRUE;
            break;
        }
    }

    if (!any_changed)
        return;

    EnableWindow(data->hList, FALSE);
    EnableWindow(data->hOk, FALSE);
    EnableWindow(data->hCancel, FALSE);
    ShowWindow(data->hProgress, SW_SHOW);
    SendMessageW(data->hProgress, PBM_SETRANGE32, 0, 100);
    SendMessageW(data->hProgress, PBM_SETPOS, 0, 0);

    for (i = 0; i < data->item_count; i++)
    {
        BOOL checked = get_listview_check_state(data->hList, i);
        BOOL was_installed = (data->items[i].initial_state == DismStateInstalled);

        if (checked && !was_installed)
        {
            WCHAR msg[128];
            swprintf(msg, ARRAY_SIZE(msg), L"Enabling %s...", data->items[i].display_name);
            SetWindowTextW(data->hStatus, msg);
            UpdateWindow(data->hDlg);

            DismEnableFeature(data->session, data->items[i].name, NULL, DismPackageNone,
                              FALSE, NULL, 0, TRUE, NULL, progress_callback, data);
            data->items[i].initial_state = DismStateInstalled;
        }
        else if (!checked && was_installed)
        {
            WCHAR msg[128];
            swprintf(msg, ARRAY_SIZE(msg), L"Disabling %s...", data->items[i].display_name);
            SetWindowTextW(data->hStatus, msg);
            UpdateWindow(data->hDlg);

            DismDisableFeature(data->session, data->items[i].name, NULL, FALSE,
                               NULL, progress_callback, data);
            data->items[i].initial_state = DismStateNotPresent;
        }
    }

    SetWindowTextW(data->hStatus, L"Completed successfully.");
    UpdateWindow(data->hDlg);
}

static INT_PTR CALLBACK OptionalFeaturesDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct dialog_data *data = (struct dialog_data *)GetWindowLongPtrW(hDlg, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        LVCOLUMNW col;
        RECT rc;
        HRESULT hr;

        data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct dialog_data));
        if (!data)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)data);

        data->hDlg = hDlg;
        data->hList = GetDlgItem(hDlg, IDC_FEATURESLIST);
        data->hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
        data->hStatus = GetDlgItem(hDlg, IDC_STATUS);
        data->hDesc = GetDlgItem(hDlg, IDC_DESCRIPTION);
        data->hOk = GetDlgItem(hDlg, IDOK);
        data->hCancel = GetDlgItem(hDlg, IDCANCEL);

        ShowWindow(data->hProgress, SW_HIDE);

        SendMessageW(data->hList, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

        GetClientRect(data->hList, &rc);
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_WIDTH;
        col.cx = rc.right - GetSystemMetrics(SM_CXVSCROLL) - 4;
        (void)ListView_InsertColumnW(data->hList, 0, &col);

        hr = DismInitialize(DismLogErrorsWarningsInfo, NULL, NULL);
        if (FAILED(hr) && hr != DISMAPI_E_DISMAPI_ALREADY_INITIALIZED)
        {
            WINE_ERR("DismInitialize failed: 0x%08lx\n", hr);
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        hr = DismOpenSession(DISM_ONLINE_IMAGE, NULL, NULL, &data->session);
        if (FAILED(hr))
        {
            WINE_ERR("DismOpenSession failed: 0x%08lx\n", hr);
            DismShutdown();
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        populate_features(data);
        return TRUE;
    }

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lParam;
        if (data && hdr && hdr->idFrom == IDC_FEATURESLIST)
        {
            if (hdr->code == LVN_ITEMCHANGED)
            {
                NMLISTVIEW *nmlv = (NMLISTVIEW *)lParam;
                if ((nmlv->uNewState & LVIS_SELECTED) && nmlv->iItem >= 0 &&
                    nmlv->iItem < (int)data->item_count)
                {
                    SetWindowTextW(data->hDesc, data->items[nmlv->iItem].description);
                }
            }
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (data)
                apply_changes(data);
            EndDialog(hDlg, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (data)
        {
            if (data->session)
                DismCloseSession(data->session);
            DismShutdown();

            if (data->items)
                HeapFree(GetProcessHeap(), 0, data->items);
            HeapFree(GetProcessHeap(), 0, data);
            SetWindowLongPtrW(hDlg, DWLP_USER, 0);
        }
        break;
    }

    return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_OPTIONAL_FEATURES), NULL, OptionalFeaturesDlgProc);
    return 0;
}
