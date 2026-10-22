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
