# wine-ultimate: Wine Staging Patches for Modern Windows Apps & UWP/WinUI 3

Experimental Wine Staging patch tree focused on reverse-engineering and implementing modern Windows NT kernel and userland subsystems required by **WinUI 3**, **Windows App SDK (WASDK)**, and native UWP runtimes.

Developed during native bring-up for **WhatsApp for Windows** (`v2.2620.102.0`, the final native WinUI 3 / XAML line using `WindowsAppRuntime.1.6` and `Microsoft.UI.Xaml 3.0.0.2411`).

**Base**: Wine Staging **v11.10+ / v11.19** (`11dd86bd`) / Upstream Wine `a15ee2c`.

---

## Current Status: WinUI 3 & WhatsApp Desktop On Screen

* **Process State**: Running stably (`WhatsApp.Root.exe`, PID active, 540MB+ RSS, 67+ threads, SQLite database generated, Rust logger active).
* **Display / Window**: Native WinUI 3 X11 window is mapped on screen: `0x9c00001 "WinUI Desktop" (952x734+4+30)`.
* **XAML & Localization**: `WhatsApp.Strings`, `Windows.ApplicationModel.Resources.ResourceLoader` (with exact .NET WinRT GUIDs `{08524908-16ef-45ad-a602-293637d7e61a}` and `{0cc04141-6466-4989-9494-0b82dfc53f1f}`), and `ApplicationLanguages.ManifestLanguages` functional without XAML parser errors.
* **WebView2 & DirectComposition**: Edge WebView2 environment and `ICoreWebView2CompositionController` initialize with `S_OK` (0x00000000).
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
* **Patches (0001–0032)**:
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
