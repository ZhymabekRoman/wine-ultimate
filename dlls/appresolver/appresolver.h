/*
 * AppResolver (Start Menu Cache and App Resolver) COM interfaces
 *
 * Vtable layout pinned from binary evidence (Microsoft.Internal.FrameworkUdk.dll):
 *  - CLSID constant at 0x18009a8a0, referenced by ToastNotifications_RetrieveAssets_Impl
 *  - winrt::guid_of<IApplicationResolver>() == IID_IApplicationResolver at 0x18009c950
 *  - calls observed on the returned object:
 *      vtable+56 (slot 7): fn(this, GetCurrentProcessId(), &appid)  -> GetAppIDForProcess
 *      vtable+72 (slot 9): fn(this, exe_path, &shell_item, &appid)  -> internal onecore method
 *      vtable+40 (slot 5): fn(this, NULL, &str)                     -> internal onecore method
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
    /*  3 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForWindow)(IApplicationResolver *This, HWND hwnd,
            PWSTR *appid, BOOL *relaunch_required, DWORD *host_type, DWORD *reliability);
    /*  4 */ HRESULT (STDMETHODCALLTYPE *GetWindowFromAppID)(IApplicationResolver *This,
            const WCHAR *appid, HWND *hwnd);
    /*  5 */ HRESULT (STDMETHODCALLTYPE *GetFirstWindowFromAppID)(void);
    /*  6 */ HRESULT (STDMETHODCALLTYPE *GetWindowFromWindow)(void);
    /*  7 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForProcess)(IApplicationResolver *This,
            DWORD pid, PWSTR *appid);
    /*  8 */ HRESULT (STDMETHODCALLTYPE *RelaunchApplication)(void);
    /*  9 */ HRESULT (STDMETHODCALLTYPE *SetWindowFromAppID)(void);
    /* 10 */ HRESULT (STDMETHODCALLTYPE *GetIconPath)(void);
    /* 11 */ HRESULT (STDMETHODCALLTYPE *GetShortNamePath)(void);
    /* 12 */ HRESULT (STDMETHODCALLTYPE *GetAppIDForCurrentProcess)(void);
    /* 13 */ HRESULT (STDMETHODCALLTYPE *reserved_13)(void);
    /* 14 */ HRESULT (STDMETHODCALLTYPE *reserved_14)(void);
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
#define IApplicationResolver_GetAppIDForWindow(p,a,b,c,d,e) (p)->lpVtbl->GetAppIDForWindow(p,a,b,c,d,e)
#define IApplicationResolver_GetWindowFromAppID(p,a,b) (p)->lpVtbl->GetWindowFromAppID(p,a,b)
#define IApplicationResolver_GetAppIDForProcess(p,a,b) (p)->lpVtbl->GetAppIDForProcess(p,a,b)
#endif

DEFINE_GUID(CLSID_StartMenuCacheAndAppResolver,
    0x660b90c8, 0x73a9, 0x4b58, 0x8c, 0xae, 0x35, 0x5b, 0x7f, 0x55, 0x34, 0x1b);
/* binary-verified IID (FrameworkUdk winrt::guid_of<IApplicationResolver>) */
DEFINE_GUID(IID_IApplicationResolver,
    0xde25675a, 0x72de, 0x44b4, 0x93, 0x73, 0x05, 0x17, 0x04, 0x50, 0xc1, 0x40);

#endif /* __WINE_APPRESOLVER_H */
