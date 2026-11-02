@ stdcall -private DllCanUnloadNow()
@ stdcall -private DllGetActivationFactory(ptr ptr)
@ stdcall -private DllGetClassObject(ptr ptr ptr)
@ stdcall -private DllRegisterServer()
@ stdcall -private DllUnregisterServer()
@ stdcall Windowing_DllGetActivationFactory(ptr ptr) DllGetActivationFactory
@ stdcall Windowing_GetDisplayIdFromMonitor(ptr ptr)
@ stdcall Windowing_GetIconFromIconId(int64 ptr)
@ stdcall Windowing_GetIconIdFromIcon(ptr ptr)
@ stdcall Windowing_GetMonitorFromDisplayId(ptr ptr)
@ stdcall Windowing_GetWindowFromWindowId(int64 ptr)
@ stdcall Windowing_GetWindowIdFromWindow(ptr ptr)
@ stub CreateControlInput
@ stub CreateControlInputEx
@ stdcall CoreWindow_Create() core_window_create

# The shell's UAP window factory ends here: it gives a view's CoreWindow its title and geometry.
# wineuwp's in-process CoreWindowBroker stand-in reaches it the same way (by ordinal, as Windows'
# twinapi.appcore does), which is why the ordinal is pinned.
1500 stdcall -private PrivateCreateCoreWindow(long wstr long long long long long ptr ptr ptr)
