# wine-ultimate: Wine Staging Patches for Modern Windows Apps & UWP/WinUI 3

Experimental Wine Staging patch tree focused on reverse-engineering and implementing modern Windows NT kernel and userland subsystems required by **WinUI 3**, **Windows App SDK (WASDK)**, and native UWP runtimes.

Developed during native bring-up for **WhatsApp for Windows** (`v2.2620.102.0`, the final native WinUI 3 / XAML line using `WindowsAppRuntime.1.6` and `Microsoft.UI.Xaml 3.0.0.2411`).

**Base**: Wine Staging **v11.10+ / v11.19** (`11dd86bd`) / Upstream Wine `a15ee2c`.

---

## Current Status: WinUI 3 & WhatsApp Desktop On Screen

* **Process State**: Running stably (`WhatsApp.Root.exe`, PID active, 540MB+ RSS, 67+ threads, SQLite database generated, Rust logger active).
* **Display / Window**: Native WinUI 3 X11 window is mapped on screen: `0x9c00001 "WinUI Desktop" (952x734+4+30)`.
* **XAML & Localization**: `WhatsApp.Strings`, `Windows.ApplicationModel.Resources.ResourceLoader` (with exact .NET WinRT GUIDs `{08524908-16ef-45ad-a602-293637d7e61a}` and `{0cc04141-6466-4989-9494-0b82dfc53f1f}`), and `ApplicationLanguages.ManifestLanguages` functional without XAML parser errors.
* **WebView2 & DirectComposition**: DirectComposition/DComp composition is live and `msedgewebview2.exe` spawns from the standalone `WebView2Loader.dll` path (verified process launch, populated `EBWebView` user-data dir). It does **not** spawn from the WhatsApp `Microsoft.UI.Xaml.Controls.WebView2` path: the loader is never loaded there because the app dies earlier in the `Microsoft.UI.Windowing.AppWindow`/`IAppWindowTitleBar` projection (`CLASS_E_CLASSNOTAVAILABLE`, see the patch 0044 notes), so no `CoreWebView2Environment` is created on that path.
* **Graphics & Surface Pipeline**: Cross-process GPU surface sharing (`IDXGIResource::GetSharedHandle` / `ID3D11Device1::OpenSharedResource1`), top-level HWND coordinate mapping (`GetAncestor(GA_ROOT)`), and DComp compositor clock driven.

---

## Reverse-Engineered Patch Sets

### 1. `ntdll-NtAlpcCreatePort`
* **Path**: `patches/ntdll-NtAlpcCreatePort/`
* **Target Subsystem**: NTDLL & Wineserver ALPC Engine
* **Components**:
  * Implements missing ALPC (Advanced Local Procedure Call) kernel interfaces:
    * `NtAlpcCreatePort`
    * `NtAlpcConnectPort`
    * `NtAlpcAcceptConnectPort`
    * `NtAlpcDisconnectPort`
    * `NtAlpcSendWaitReceivePort`
  * New `server/alpc.c` server object and wineserver protocol requests (`create_alpc_port`, `connect_alpc_port`, `accept_alpc_connection`, `send_wait_receive_alpc_port`).
  * Roundtrip-verified message queueing, connection state machine, attribute negotiation, and context passing.
* **Why Required**: CoreMessaging (`CoreMessagingXP.dll` / `CoreMessaging.dll`) in Windows App SDK uses ALPC endpoints for in-process and inter-process message dispatch and registrar services.

### 2. `ntdll-WaitCompletionPacket`
* **Path**: `patches/ntdll-WaitCompletionPacket/`
* **Target Subsystem**: NTDLL & Wineserver Async Wait Completion Engine & ALPC Message Bus
* **Depends on**: `ntdll-NtAlpcCreatePort`
* **Patches (0001–0010)**:
  * `0001`: Implements `NtCreateWaitCompletionPacket`, `NtAssociateWaitCompletionPacket`, and `NtCancelWaitCompletionPacket` + wineserver completion packet object.
  * `0002`: Emulates Windows kernel ALPC connection handshake behavior expected by `CoreMessagingXP.dll`, status `0x87b2080c`, and flag byte checks at offset `+0x3a`.
  * `0003`: Transports ALPC connection-message payload and `ClientId`.
  * `0004`: Implements `NtAlpcImpersonateClientOfPort` (in-process impersonation).
  * `0005`: ALPC post-accept message routing + per-client `PortContext` and `ClientId` delivery.
  * `0006`: Stamps ALPC message `Type` and assigns `MessageId`; fixes send use-after-free.
  * `0007`: Corrects per-message `PortContext` for reply direction.
  * `0008`: Implements `OBJ_OPENIF` named port reuse and correlated synchronous request/reply matching by `MessageId` in wineserver async queue.
  * `0009`: Server-side multi-connection reply routing by `MessageId` across multiple client connection endpoints on the listening port.
  * `0010`: Routes client data messages directly to connected communication `peer` rather than fallback listening port, resolving `0x87B2000C` validate buffer error in `CoreMessagingXP.dll` during "Lifted" endpoint registration.

### 3. `kernelbase-PackageIdentity`
* **Path**: `patches/kernelbase-PackageIdentity/`
* **Target Subsystem**: KernelBase / AppModel & Wineserver Object Handles
* **Patches (0001–0004)**:
  * `0001`: Reports package identity parsed from environment variables (`PACKAGE_FAMILY_NAME`, `PACKAGE_FULL_NAME`) for unpackaged / standalone runtime launch.
  * `0002`: Implements `GetCurrentPackageId`, `GetCurrentPackageInfo`, `GetCurrentPackagePath`, and `GetPackagePathByFullName`.
  * `0003`: `AppPolicyGetWindowingModel` returns `AppPolicyWindowingModel_Universal` for packaged apps to bypass AppContainer sandboxing checks in CoreMessaging.
  * `0004`: Allows in-process handle access right escalation (`entry->access |= access`) in wineserver to eliminate false-positive `STATUS_ACCESS_DENIED` (`0xc0000022` / `0xd0000022`), and makes `CloseHandle` non-fatal on invalid/unclosable handles to avoid WIL failfast assertions in `FrameworkUdk.dll`.

### 4. `twinapi-appcore-UWP`
* **Path**: `patches/twinapi-appcore-UWP/`
* **Target Subsystem**: `twinapi.appcore.dll`, `windows.ui.dll`, AppLifecycle, Dispatcher, Connectivity, Devices, Cryptography, Media, Calls
* **Depends on**: `kernelbase-PackageIdentity`
* **Patches (0001–0043)**:
  * `0001–0014`: Implements `ICoreApplication`, `CoreApplicationView`, `CoreWindow` with Win32 HWND backing, `WS_VISIBLE`, and UWP activation args.
  * `0015`: Implements `VisibilityChangedEventArgs` and `WindowSizeChangedEventArgs` in `windows.ui.dll`.
  * `0016`: Implements `IPackageId`, `IBackgroundExecutionManagerStatics`, `IPushNotificationChannelManagerStatics4`, and `container_CreateContainer`.
  * `0017`: Comprehensive bridge capturing `twinapi`, `cryptowinrt`, `frameworkudk.system`, `shcore` feature staging, `kernelbase`, `combase`, and `ntdll` synchronization deadlock avoidance.
  * `0018`: Implements `Windows.Networking.Connectivity` dummy `ConnectionProfile` (`InternetAccess` level 3, unrestricted cost) and `HostName` enumerator bound to dummy network adapter `{11111111-2222-3333-4455-66778899aabb}`, unblocking socket connection dispatcher.
  * `0019`: Implements `Windows.Devices.Enumeration` (`DeviceWatcher`, `CreateWatcherDeviceClass`, `FindAllAsyncDeviceClass`, `CreateFromIdAsync`) and `Windows.Media.Devices` selectors and default audio endpoint fallbacks, resolving VoIP `-2147467263` (`E_NOTIMPL`) initialization failure.
  * `0020`: Implements Chromium sandbox Job Object limits (`JobObjectExtendedLimitInformation`, `JobObjectBasicLimitInformation`, `JobObjectSecurityLimitInformation`) in `kernelbase` and `ntdll`, plus `CreateRestrictedToken` with `DISABLE_MAX_PRIVILEGE`.
  * `0021`: Implements `Windows.Security.Cryptography.Core` (`CryptographicEngine`, `CryptographicKey`, `EncryptedAndAuthenticatedData`, `SymmetricKeyAlgorithmProvider` supporting AES-GCM, `CryptographicHash`, and `CryptographicBuffer`).
  * `0022`: Implements `Windows.Media.Transcoding` (`MediaTranscoder`), `Windows.Media.Editing` (`MediaComposition`, `MediaClip` with thumbnail stream for WIC), and `Windows.ApplicationModel.Calls` (`VoipCallCoordinator`, `VoipPhoneCall`).
  * `0023`: Implements `Windows.ApplicationModel.DataTransfer` (`DataTransferManager`, `DataPackage`, `Clipboard`, `HtmlFormatHelper`).
  * `0024`: Implements `Windows.Globalization` (`ApplicationLanguages.ManifestLanguages`, `Language.DisplayName`, `NativeName`, `Script`).
  * `0025`: Implements `Windows.Web.Http` (`HttpClient`, `HttpRequestMessage`, `HttpResponseMessage`, `HttpStringContent`).
  * `0026`: Implements `Windows.Networking.Sockets` (`StreamSocket`, `MessageWebSocket`).
  * `0027`: Implements `Windows.Media.Audio` (`AudioGraph`, `AudioDeviceInputNode`, `AudioDeviceOutputNode`).
  * `0028`: Implements `Windows.System.Profile.HardwareIdentification` (`GetPackageSpecificToken`) and `IBackgroundTaskRegistrationStatics::get_AllTasks`.
  * `0029`: Implements `Windows.Storage` and `Windows.Storage.Streams` IDL runtimeclasses and interfaces (`StorageFile`, `StorageFolder`, `IStorageFileStatics`, `IStorageFolderStatics`, `IDataReader`, `IDataReaderFactory`, `IStreamedFileDataRequest`).
  * `0030`: Implements `Windows.Media.Capture` IDL definitions (`MediaCapture`, `MediaCaptureInitializationSettings`, `IMediaCapture`, `IMediaCaptureVideoPreview`, `IMediaCapture5`).
  * `0031`: Implements `Windows.Media.Devices` IDL definitions (`VideoDeviceController`, `MediaDeviceControl`, `MediaDeviceControlCapabilities`, `IVideoDeviceController`, `IMediaDeviceControl`).
  * `0032`: Implements parameterized WinRT collection interfaces for `BackgroundTaskRegistration.AllTasks` (`IMapView`, `IIterable`, `IIterator`) with strict COM identity and `GetIids`, and implements `Windows.Globalization` `IVectorView` and `IIterator` runtime class names and `IInspectable` methods.
  * `0033`: Implements clean multi-interface COM vtable separation for `ResourceLoader` and `ResourceLoaderFactory` (`IResourceLoader`, `IResourceLoader2`, `IActivationFactory`, `IResourceLoaderFactory`, `IResourceLoaderStatics`, `IResourceLoaderStatics2`, `IResourceLoaderStatics3`), creates `elscore.dll` for Extended Linguistic Services Core, un-comments `gdi32.ScriptSubstituteSingleGlyph` export for `USP10.dll` forward, routes `ext-ms-win-windowing-internal-l1-1-0` apiset to `windows.ui.dll`, and exports `Windowing_DllGetActivationFactory`.
  * `0034`: Implements `Windows.Storage` and `Windows.Storage.Streams` runtime classes (`StorageFile`, `StorageFolder`, `FileIO`, `PathIO`, `Buffer`, `DataReader`, `DataWriter`, `InMemoryRandomAccessStream`, `async.c`), unblocking WinAppSDK file and stream I/O.
  * `0035`: Implements Chromium sandbox token privilege filtering (`DISABLE_MAX_PRIVILEGE` in `server/token.c` and `NtFilterToken`) and Windows JobObject limit support (`JobObjectExtendedLimitInformation`, `JobObjectBasicLimitInformation`, `JobObjectSecurityLimitInformation`, `JobObjectBasicUIRestrictions`) in `server` and `ntdll`.
  * `0036`: Implements cross-process GPU surface sharing and DComp DXGI swapchain composition engine across `server/d3dkmt.c`, `dxgi`, `d3d11`, and `dcomp`, enabling offscreen Edge WebView2 swapchains to blit to top-level X11 windows.
  * `0037`: Implements the `bcp47mrm.dll` BCP-47 language tag parser and conformance test suite: all 17 exports (`IsWellFormedTag`, `IsValidTag`, `Bcp47IsValid` with full RFC 5646 grammar plus documented Windows leniencies — standalone private-use sequences, 4-digit region codes, 4-character mixed alphanumeric variants, all-digit 5-8 character variants — and MRM table helpers as honest FIXME stubs), with a standard Wine `wine/test.h` test suite in `dlls/bcp47mrm/tests/` covering 117 tag cases (valid, invalid, subtag ordering, extensions, private-use, 0x55 length boundary) on x86_64 and i386.
  * `0038`: Implements `Windows.System.Power.PowerManager` IDL definitions (`IPowerManagerStatics` UUID `{1394825D-62CE-4364-98D5-AA28C7FBD15B}`, `BatteryStatus`, `PowerSupplyStatus`, `EnergySaverStatus`, `runtimeclass PowerManager`) and clean thread-safe statics implementation in `dlls/twinapi.appcore/power_manager.c` returning desktop defaults (`BatteryStatus` NotPresent, `PowerSupplyStatus` Adequate, `RemainingChargePercent` 100, `EnergySaverStatus` Off) and valid event registration tokens.
  * `0039`: Adds standard `wine/test.h` conformance test suites for `Windows.Web.Http` (RoGetActivationFactory for `HttpClient`/`HttpRequestMessage`/`HttpResponseMessage`/`HttpStringContent`, `IHttpClient`/`IHttpClient2` methods, `IAsyncOperationWithProgress<HttpResponseMessage, HttpProgress>` and `IAsyncInfo` vtable conformance, header and content string manipulation — 619 assertions), `Windows.Networking.Sockets` (new `dlls/windows.networking.sockets/tests/`: `StreamSocket` and `MessageWebSocket` activation, interface queries, `ConnectAsync` returning `IAsyncAction` with `IAsyncInfo` — 159 assertions), and `cryptowinrt` (`CryptographicBuffer` `CreateFromByteArray`/`CopyToByteArray`/`Compare`, `SymmetricKeyAlgorithmProvider` `OpenAlgorithm`/`CreateSymmetricKey`, `CryptographicEngine` `Encrypt`/`Decrypt`/`SignHashedData`/`EncryptAndAuthenticate`/`DeriveKeyMaterial`, `HashAlgorithmProvider` SHA-256 digest, `DataProtectionProvider` `ProtectAsync`/`UnprotectAsync` roundtrip — 309 assertions); fixes ABI conformance defects the suites exposed: `GetRuntimeClassName` HSTRING lengths, `HttpStringContent` `IStringable` vtable `impl_from` offset, UTF-8 `TryComputeLength`, `IStreamSocket` ConnectAsync vtable order and split `IStreamSocketStatics`, stdcall-correct setter stubs (i386 stack corruption). All suites pass on x86_64 and i386.
  * `0040`: Fixes Chromium/WebView2 Mojo named-pipe IPC semantics: real client-token impersonation in wineserver (`FSCTL_PIPE_IMPERSONATE` duplicates the connecting client's process token held in `struct pipe_end.client_proc` with proper refcount lifecycle across connect/disconnect/destroy, replacing the server-token `FIXME` stub), honors `FILE_FLAG_FIRST_PIPE_INSTANCE` (mapped through `CreateNamedPipeW` → `NtCreateNamedPipeFile` into a new `NAMED_PIPE_FIRST_INSTANCE` request flag, `STATUS_ACCESS_DENIED` on name collision, matching npfs), and completes `GetNamedPipeHandleStateW` (populates `lpMaxCollectionCount` from `FILE_PIPE_LOCAL_INFORMATION.MaximumInstances` and `lpCollectDataTimeout` from the documented `NMPWAIT_WAIT_FOREVER` default instead of returning zeros, removing the semi-stub `FIXME`).
  * `0041`: Implements `IUISettings6` (`windows.ui.dll`, `Windows.UI.ViewManagement.UISettings`, UniversalApiContract 10.0): `add/remove_AnimationsEnabledChanged` and `add/remove_MessageDurationChanged` event-registration pairs, wired into `UISettings` `QueryInterface` and returned from `ActivateInstance`. WinUI 3 `Microsoft.UI.Xaml`/`Microsoft.UI.Windowing` QIs the `UISettings` instance for `IUISettings6` during window initialization; with only `IUISettings`–`IUISettings5` present the query returns `E_NOINTERFACE`, C++/WinRT raises it as a `COMException` via `RoOriginateErrorW`, and WinUI3 apps (WhatsApp Desktop) abort their startup path and show their fatal dialog instead of bootstrapping `CoreWebView2Environment`/`msedgewebview2.exe`.
  * `0042`: Adds the standard Wine `wine/test.h` ALPC conformance test suite (extends `dlls/ntdll/tests/alpc.c`): `NtAlpcCreatePort` (null-pointer probe, named port creation, handle duplication with `DUPLICATE_SAME_ACCESS`, name collision), `NtAlpcConnectPort` (null probe, `STATUS_OBJECT_NAME_NOT_FOUND` on missing port), `NtAlpcAcceptConnectPort` (invalid handle), `NtAlpcSendWaitReceivePort` (invalid handle both phases, no-op, receive timeout), full threaded connect/accept round-trip with connection-message payload delivery, `ClientId`/`MessageId` kernel stamping, `STATUS_BUFFER_TOO_SMALL` retry semantics, request/reply data integrity, `ALPC_MESSAGE_CONTEXT_ATTRIBUTE` `PortContext` propagation from `NtAlpcAcceptConnectPort` to both server receive and client reply receive, connection rejection (`accept_connection = FALSE` -> `STATUS_CONNECTION_REFUSED`), disconnect + handle-close cleanup; also implements `NtAlpcSetInformation` (`AlpcPortSecurityDescriptorInformation` -> `NtSetSecurityObject`, exports in `ntdll.spec`/`ntsyscalls.h`, declaration in `winternl.h`). The port-lifecycle suite is `#ifdef _WIN64` because the Wine ALPC wire protocol is 64-bit only; attribute-helper tests run on both. Passes 1640 assertions on x86_64 and 1541 on i386 with zero failures.
  * `0043`: Fixes the remaining WinAppSDK `QueryInterface` failures that blocked the WebView2 bootstrap. `twinapi.appcore`: answers `IAgileObject` on the `PushNotificationChannelManager` and `BackgroundExecutionManager` factories (the agile-reference machinery refused the freshly resolved factory, `PushNotificationChannelManager` aborted before creating its channel and the app surfaced its fatal `HybridDialog*` resources); names `IPushNotificationChannelManagerStatics` `{8baf9b65-77a1-4588-bd19-861529a9dcf0}` as a GUID constant instead of comparing raw GUID bytes with `memcmp`; answers `IWeakReferenceSource` on the push-channel/startup-task/background-task-builder stub with the weak-reference-source object the file already implements; reports the interfaces that stay unimplemented (`IMarshal` is not needed by an agile factory) through `FIXME`. `coremessaging`: answers the Windows App SDK `Microsoft.*` namespace aliases `IMicrosoftDispatcherQueue` `{f6ebf8fa-be1c-5bf6-a467-73da28738ae8}`, `IMicrosoftDispatcherQueueController` `{bce8178d-2183-584c-9e5b-f9366f6ae484}`, `IMicrosoftDispatcherQueueControllerStatics` `{f18d6145-722b-593d-bcf2-a61e713f0037}` and adds the `IDispatcherQueueController2` `{4c68ee2a-1cb1-5591-a3a2-9b590b8f8b9a}` stub. Both DLLs answer `IReferenceTracker` `{11d3b13a-180e-4789-a8be-7712882893e6}` and `IReferenceTrackerTarget` `{5c13e51c-4f32-4726-a3fd-f3edd63da3a0}` explicitly with `E_NOINTERFACE`: the reference tracker coordinates native/managed object lifetime, and answering `IReferenceTrackerTarget` makes .NET `ComWrappers.GetOrCreateObjectForComInstance` throw `NotImplementedException`, which is fatal in `AppInstance.FindOrRegisterForKey` inside `WhatsApp.Program.DecideRedirection`. Also removes the leftover `ERR` debug tracing from `dispatcher_queue_TryEnqueue`.
  * `0044`: Makes every WinRT activation factory agile. `RoGetActivationFactory` hands out an agile object on Windows, and C++/WinRT, CsWinRT and the Windows App SDK `CFlat` projection resolve an agile reference (`IAgileObject` `{94ea2b94-e9cc-49e0-c0ff-ee64ca8f5b90}`) on the factory before using it. Seventeen factory `QueryInterface` implementations in the tree listed only `IUnknown`/`IInspectable`/`IActivationFactory`, so the agile-reference query returned `E_NOINTERFACE` and the caller discarded a factory it had just resolved. WhatsApp Desktop hits this during window initialisation: the `Windows.UI.ViewManagement.UISettings` and `AccessibilitySettings` factories (`windows.ui.dll`) were refused `IAgileObject` in the DPI/theme lookup of the WinUI 3 window, and the four `fixme:ui:factory_QueryInterface {94ea2b94-…} not implemented` lines disappear after the fix. Audited and fixed across `windows.ui` (uisettings, accessibilitysettings, uiviewsettings, inputpane), `windows.graphics`, `windows.ui.core.textinput`, `twinapi.appcore` (client_device_information, data_transfer), `windows.devices.enumeration` (main, access), `windows.media` (captions, transcoder), `windows.media.playback.mediaplayer`, `windows.system.profile.systemid`, `windows.security.authentication.onlineid`, `windows.security.credentials.ui.userconsentverifier` and `appxdeploymentclient`, continuing the rule 0043 set for `twinapi.appcore`/CoreMessaging. Interfaces that must stay unimplemented on a factory (`IMarshal`, and the reference tracker interfaces per 0043) are unchanged. Conformance tests updated to the correct expectation: uisettings 82/82, graphics 9/9, twinapi 137/137, media 45/45, systemid 26/26, textinput 35/35, mediaplayer 20/20 pass; the two factory assertions in `windows.devices.enumeration` pass and its remaining failures are the pre-existing no-enumerable-devices ones.
* **Remaining blocker (traced, not fixed)**: `msedgewebview2.exe` is still not spawned by the WhatsApp path. The decisive measurement is that `WebView2Loader.dll` is never loaded in the WhatsApp process at all (full `+loaddll` trace: zero occurrences, and no `EBWebView`/user-data-dir probing), so the failure is upstream of the loader. The last Wine-visible error is a .NET `System.Runtime.InteropServices.COMException` with `0x80040111` (`CLASS_E_CLASSNOTAVAILABLE`) originated through `combase!RoOriginateErrorW`; mapping its `originate_bt` frames to module offsets resolves the origin to `Microsoft.UI.Windowing.dll!Api::IAppWindow::ExportAdapter$::get_TitleBar` (+0x1ec45, reached from `Microsoft.ui.xaml.dll`) — the `CFlat` export adapter for `AppWindow.get_TitleBar` asks the managed peer object's interface dispatcher for `IAppWindow` and marshals `Microsoft.UI.Windowing.IAppWindowTitleBar` `{5574efa2-c91c-5700-a363-539c71a7aaf4}`, the managed side throws, the native adapter propagates the `HRESULT`, and the app falls into its fatal `HybridDialog*` dialog. The standalone `test_wv2.exe` path (direct `WebView2Loader.dll` call) still spawns Edge, which isolates the defect to the .NET ↔ `CFlat` composable-class activation of `Microsoft.UI.Windowing.AppWindow` — not to the loader, browser discovery, registry (`EdgeUpdate\Clients\{F3017226-…}` `pv=155.0.4283.62` is present) or process launch.

### 5. `dcomp-DCompositionCreateDevice2`
* **Path**: `patches/dcomp-DCompositionCreateDevice2/`
* **Target Subsystem**: DirectComposition (`dcomp.dll`), `dxgi.dll`, `d3d11.dll`, `user32.dll`, `win32u`
* **Patches (0001–0076)**:
  * `0001–0066`: Base CodeWeavers DComposition patchset (Zhiyi Zhang).
  * `0067`: Exports 11 `DCompPrivates_*` stubs needed for `Microsoft.Internal.FrameworkUdk.dll` delay-import resolution.
  * `0068–0072`: Prevents premature compositor worker thread termination on empty initial tick, fixes compositor clock, container map view, CoreWindow `CS_OWNDC`, and SEH logging.
  * `0073`: Implements composition input sink syscalls (`NtCreateCompositionInputSink`, `NtCreateImplicitCompositionInputSink`, `NtDuplicateCompositionInputSink`, `NtQueryCompositionInputSinkLuid`) in `win32u`, `dxgi_device_EnqueueSetEvent`, and private user32 exports (ordinals 2503, 2505, 2557, and `GetWindowBand`).
  * `0074`: Implements `desktop_device_CreateSharedResource` and `desktop_device_OpenSharedResourceHandle` for `IDCompositionVisual`, and `composition_device_create_surface` with dynamic D3D11 rendering device acquisition, unblocking Edge `CreateCoreWebView2CompositionController`.
  * `0075`: Fixes top-level HWND presentation by resolving root window via `GetAncestor(target->hwnd, GA_ROOT)`, mapping child coordinates via `MapWindowPoints`, and recursively traversing `shared_visual_handle` visual trees.
  * `0076`: Implements cross-process shared resource engine (`IDXGIResource::GetSharedHandle`, `IDXGIResource1::CreateSharedHandle`, `ID3D11Device::OpenSharedResource`, `ID3D11Device1::OpenSharedResource1`, and wineserver D3DKMT cross-process handle lookup).

---

## How to Apply Patches

Using Wine Staging `patchinstall.py`:

```bash
# Apply full WASDK / WinUI 3 stack to Wine tree:
./staging/patchinstall.py DESTDIR="/path/to/wine" \
    ntdll-NtAlpcCreatePort \
    ntdll-WaitCompletionPacket \
    combase-AgileObject \
    coremessaging-DispatcherQueue \
    kernelbase-PackageIdentity \
    profapi-UserToken \
    windows.graphics-DisplayInformation \
    twinapi-appcore-UWP \
    dcomp-DCompositionCreateDevice2
```

---

## Repository & Branches

* **Current Base**: Synced to Wine-Staging `v11.19` (`11dd86bd`)
* **Upstream Wine**: `https://gitlab.winehq.org/wine/wine.git`
* **Wine-Staging**: `https://gitlab.winehq.org/wine/wine-staging.git`
* **GitHub**: `https://github.com/ZhymabekRoman/wine-ultimate`
