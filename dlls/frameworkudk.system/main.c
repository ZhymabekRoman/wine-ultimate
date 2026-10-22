#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winerror.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(frameworkudk);

/*
 * CoreUdk function-table slots that Wine has no entry for.  The Windows App
 * SDK calls these unconditionally on the XAML startup path
 * (Microsoft.UI.Xaml.dll!Application.Start -> CoreUdk table slots) and treats
 * a failure as fatal, so an unimplemented slot answers success without
 * touching its out-parameters; measured: answering E_NOTIMPL here stops
 * WhatsApp.Root.exe inside Application.Start with an unhandled
 * System.NotImplementedException.  Entries with a Wine implementation are
 * listed explicitly in init_tables().
 */
static HRESULT WINAPI default_stub(void)
{
    return S_OK;
}

static HRESULT WINAPI win_GetWindowIdFromWindow(HWND hwnd, UINT64 *window_id)
{
    TRACE("hwnd %p, window_id %p\n", hwnd, window_id);
    if (!window_id) return E_POINTER;
    if (!hwnd)
    {
        *window_id = 0;
        return E_INVALIDARG;
    }
    *window_id = (UINT64)(ULONG_PTR)hwnd;
    return S_OK;
}

static HRESULT WINAPI win_GetWindowFromWindowId(UINT64 window_id, HWND *hwnd)
{
    TRACE("window_id %016llx, hwnd %p\n", window_id, hwnd);
    if (!hwnd) return E_POINTER;
    if (!window_id)
    {
        *hwnd = NULL;
        return E_INVALIDARG;
    }
    *hwnd = (HWND)(ULONG_PTR)window_id;
    return S_OK;
}

static HRESULT WINAPI win_GetDisplayIdFromMonitor(HMONITOR monitor, UINT64 *display_id)
{
    TRACE("monitor %p, display_id %p\n", monitor, display_id);
    if (!display_id) return E_POINTER;
    *display_id = (UINT64)(ULONG_PTR)monitor;
    return S_OK;
}

static HRESULT WINAPI win_GetMonitorFromDisplayId(UINT64 display_id, HMONITOR *monitor)
{
    TRACE("display_id %016llx, monitor %p\n", display_id, monitor);
    if (!monitor) return E_POINTER;
    *monitor = (HMONITOR)(ULONG_PTR)display_id;
    return S_OK;
}

/*
 * ResourceManager_GetDefaultResourcePathForPackageFullName (table0 entry 54).
 * Wine runs WinUI applications unpackaged, so report the process as having
 * no package identity.  MRT Core (Microsoft.Windows.ApplicationModel.Resources)
 * handles this specific failure by falling back to resources.pri next to
 * the executable, which is where unpackaged apps ship their PRI files.
 */
static HRESULT WINAPI res_GetDefaultResourcePathForPackageFullName(const void *full_name, WCHAR **path)
{
    TRACE("full_name %p, path %p\n", full_name, path);
    if (path) *path = NULL;
    return 0x80073D54; /* HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE) */
}

static HRESULT WINAPI win_GetIconIdFromIcon(HICON icon, UINT64 *icon_id)
{
    TRACE("icon %p, icon_id %p\n", icon, icon_id);
    if (!icon_id) return E_POINTER;
    *icon_id = (UINT64)(ULONG_PTR)icon;
    return S_OK;
}

static HRESULT WINAPI win_GetIconFromIconId(UINT64 icon_id, HICON *icon)
{
    TRACE("icon_id %016llx, icon %p\n", icon_id, icon);
    if (!icon) return E_POINTER;
    *icon = (HICON)(ULONG_PTR)icon_id;
    return S_OK;
}

static void *table0[64];
static void *table1[16];
static void *table2[32];
static void *table3[16];
static void *table4[32];
static void *table5[16];
static void *table6[16];
static void *table7[16];
static void *table8[16];
static void *table9[16];
static void *table10[16];
static void *table11[16];
static void *table12[16];
static BOOL tables_inited = FALSE;

static void init_tables(void)
{
    int i;
    HMODULE dcomp = GetModuleHandleW(L"dcomp.dll");
    if (!dcomp) dcomp = LoadLibraryW(L"dcomp.dll");

    for (i = 0; i < 64; i++) table0[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table1[i] = (void *)default_stub;
    for (i = 0; i < 32; i++) table2[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table3[i] = (void *)default_stub;
    for (i = 0; i < 32; i++) table4[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table5[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table6[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table7[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table8[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table9[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table10[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table11[i] = (void *)default_stub;
    for (i = 0; i < 16; i++) table12[i] = (void *)default_stub;

    table0[47] = (void *)win_GetWindowIdFromWindow;
    table0[48] = (void *)win_GetWindowFromWindowId;
    table0[49] = (void *)win_GetDisplayIdFromMonitor;
    table0[50] = (void *)win_GetMonitorFromDisplayId;
    table0[54] = (void *)res_GetDefaultResourcePathForPackageFullName; /* offset 432 */

    table4[9] = (void *)win_GetIconIdFromIcon;
    table4[10] = (void *)win_GetIconFromIconId;

    if (dcomp)
    {
        void *p;
        if ((p = (void *)GetProcAddress(dcomp, "DCompPrivates_CreateSharedVisualHandle")))
            table0[14] = p; /* offset 0x70 */
        if ((p = (void *)GetProcAddress(dcomp, "DCompPrivates_CreateCompositionBatchQuery")))
            table2[23] = p; /* offset 0xb8 */
        if ((p = (void *)GetProcAddress(dcomp, "DCompPrivates_OpenSharedDCompositionObject")))
            table3[1] = p; /* offset 0x8 */
        if ((p = (void *)GetProcAddress(dcomp, "DCompPrivates_SetCommitCompletionEvent")))
            table11[3] = p; /* offset 0x18 */
    }

    tables_inited = TRUE;
}

void * WINAPI GetFunctionTableById(UINT32 tableId, UINT32 *pSize, UINT32 *pVersion)
{
    if (!tables_inited)
        init_tables();

    TRACE("tableId %u, pSize %p, pVersion %p\n", tableId, pSize, pVersion);

    switch (tableId)
    {
    case 0:
        if (pSize) *pSize = sizeof(table0);
        if (pVersion) *pVersion = 5;
        return table0;
    case 1:
        if (pSize) *pSize = sizeof(table1);
        if (pVersion) *pVersion = 1;
        return table1;
    case 2:
        if (pSize) *pSize = sizeof(table2);
        if (pVersion) *pVersion = 1;
        return table2;
    case 3:
        if (pSize) *pSize = sizeof(table3);
        if (pVersion) *pVersion = 1;
        return table3;
    case 4:
        if (pSize) *pSize = sizeof(table4);
        if (pVersion) *pVersion = 1;
        return table4;
    case 5:
        if (pSize) *pSize = sizeof(table5);
        if (pVersion) *pVersion = 1;
        return table5;
    case 6:
        if (pSize) *pSize = sizeof(table6);
        if (pVersion) *pVersion = 1;
        return table6;
    case 7:
        if (pSize) *pSize = sizeof(table7);
        if (pVersion) *pVersion = 1;
        return table7;
    case 8:
        if (pSize) *pSize = sizeof(table8);
        if (pVersion) *pVersion = 1;
        return table8;
    case 9:
        if (pSize) *pSize = sizeof(table9);
        if (pVersion) *pVersion = 1;
        return table9;
    case 10:
        if (pSize) *pSize = sizeof(table10);
        if (pVersion) *pVersion = 1;
        return table10;
    case 11:
        if (pSize) *pSize = sizeof(table11);
        if (pVersion) *pVersion = 1;
        return table11;
    case 12:
        if (pSize) *pSize = sizeof(table12);
        if (pVersion) *pVersion = 1;
        return table12;
    default:
        if (pSize) *pSize = 0;
        if (pVersion) *pVersion = 0;
        return NULL;
    }
}
