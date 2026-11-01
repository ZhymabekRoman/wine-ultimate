/*
 * AppResolver (Start Menu Cache and App Resolver) COM interfaces
 *
 * Vtable layout pinned from binary evidence (two independent consumers, same order):
 *
 *  1) Microsoft.Toolkit.Uwp.Notifications.dll (IDA .NET metadata decode of the
 *     ComImport interface, MethodRVA slot encoding):
 *        3  GetAppIDForShortcut
 *        4  GetAppIDForShortcutObject
 *        5  GetAppIDForWindow(HWND, PWSTR*, BOOL*, DWORD*, DWORD*)
 *        6  GetAppIDForProcess(DWORD pid, PWSTR*, BOOL*, BOOL*, BOOL*)   <- 5 args
 *        7  GetShortcutForProcess
 *        8  GetBestShortcutForAppID
 *        9  GetBestShortcutAndAppIDForAppPath
 *        10 CanPinApp
 *        11 CanPinAppShortcut
 *        12 GetRelaunchProperties
 *        13 GenerateShortcutFromWindowProperties
 *        14 GenerateShortcutFromItemProperties
 *
 *  2) Microsoft.Internal.FrameworkUdk.dll!ToastNotifications_RetrieveAssets_Impl
 *     (native, calls vtable+40/+56/+72 = slots 5/7/9):
 *        slot 5: fn(this, NULL, &appid_str)                     = GetAppIDForWindow
 *        slot 7: fn(this, pid, &shell_item)  (SHGetIDListFromObject on the out)
 *                = GetShortcutForProcess
 *        slot 9: fn(this, exe_path, &shell_item, &appid_str)    = GetBestShortcutAndAppIDForAppPath
 *
 * CLSID constant at FrameworkUdk 0x18009a8a0; IID via winrt::guid_of<IApplicationResolver>()
 * at 0x18009c950: {de25675a-72de-44b4-9373-05170450c140}.
 *
 * The .NET Toolkit projection (Microsoft.Toolkit.Uwp.Notifications.Win32AppInfo.Get()
 * in ToastNotificationManagerCompat..cctor()) calls slot 6 with FIVE args
 * (pid + four out-params); the native FrameworkUdk caller declares the older
 * two-pointer shapes on slots 5/7/9, so the slot 5 implementation must write the
 * appid out-param only and never touch the extra register/stack out-slots.
 */
#ifndef __WINE_APPRESOLVER_H
#define __WINE_APPRESOLVER_H

typedef struct IApplicationResolver IApplicationResolver;

typedef struct IApplicationResolverVtbl
{
    /*** IUnknown methods ***/
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IApplicationResolver *This, REFIID riid, void **ppv);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IApplicationResolver *This);
    ULONG   (STDMETHODCALLTYPE *Release)(IApplicationResolver *This);
    /*** IApplicationResolver methods ***/
    /*  3 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForShortcut)(void);
    /*  4 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForShortcutObject)(void);
    /*  5 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForWindow)(IApplicationResolver *This, HWND hwnd,
            PWSTR *appid, BOOL *relaunch_required, DWORD *host_type, DWORD *reliability);
    /*  6 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForProcess)(IApplicationResolver *This, DWORD pid,
            PWSTR *appid, BOOL *pinning_prevented, BOOL *explicit_appid, BOOL *embedded_shortcut_valid);
    /*  7 */ HRESULT (STDMETHODCALLTYPE *GetShortcutForProcess)(void);
    /*  8 */ HRESULT (STDMETHODCALLTYPE *GetBestShortcutForAppID)(void);
    /*  9 */ HRESULT (STDMETHODCALLTYPE *GetBestShortcutAndAppIDForAppPath)(void);
    /* 10 */ HRESULT (STDMETHODCALLTYPE *CanPinApp)(void);
    /* 11 */ HRESULT (STDMETHODCALLTYPE *CanPinAppShortcut)(void);
    /* 12 */ HRESULT (STDMETHODCALLTYPE *GetRelaunchProperties)(void);
    /* 13 */ HRESULT (STDMETHODCALLTYPE *GenerateShortcutFromWindowProperties)(void);
    /* 14 */ HRESULT (STDMETHODCALLTYPE *GenerateShortcutFromItemProperties)(void);
    /* 15 */ HRESULT (STDMETHODCALLTYPE *reserved_15)(void);
} IApplicationResolverVtbl;

interface IApplicationResolver
{
    CONST_VTBL struct IApplicationResolverVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IApplicationResolver_QueryInterface(p,a,b) (p)->lpVtbl->QueryInterface(p,a,b)
#define IApplicationResolver_AddRef(p) (p)->lpVtbl->AddRef(p)
#define IApplicationResolver_Release(p) (p)->lpVtbl->Release(p)
#define IApplicationResolver_GetAppIDForWindow(p,a,b) (p)->lpVtbl->GetAppIDForWindow(p,a,b,0,0,0)
#define IApplicationResolver_GetAppIDForWindow5(p,a,b,c,d,e) (p)->lpVtbl->GetAppIDForWindow(p,a,b,c,d,e)
#define IApplicationResolver_GetAppIDForProcess(p,a,b,c,d,e) (p)->lpVtbl->GetAppIDForProcess(p,a,b,c,d,e)
#endif

DEFINE_GUID(CLSID_StartMenuCacheAndAppResolver,
    0x660b90c8, 0x73a9, 0x4b58, 0x8c, 0xae, 0x35, 0x5b, 0x7f, 0x55, 0x34, 0x1b);
/* binary-verified IID (FrameworkUdk winrt::guid_of<IApplicationResolver> == Toolkit ComImport Guid) */
DEFINE_GUID(IID_IApplicationResolver,
    0xde25675a, 0x72de, 0x44b4, 0x93, 0x73, 0x05, 0x17, 0x04, 0x50, 0xc1, 0x40);

#endif /* __WINE_APPRESOLVER_H */
