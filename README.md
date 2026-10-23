# wine-ultimate — WinUI 3 / Windows App SDK stack for Wine

**Canonical source of truth: branch [`ultimate`](../../tree/ultimate) of this repository.**
The tree there is a Wine fork: upstream Wine 11.10 plus the whole WinUI 3 / Windows App
SDK (WASDK) / DirectComposition stack as real commits.

The `patches/` directories that used to live on `main` are now a **generated artifact**.
Regenerate them from the branch with [`tools/export-series.sh`](tools/export-series.sh);
do not edit them by hand.

---

## Layout

| Path | Content |
| --- | --- |
| branch `ultimate` | canonical Wine fork source: base + series commits + WIP commits (80 commits) |
| tag `series/v0052-dcomp0077` | canonical tip of `ultimate` |
| branch `patches-archive`, tag `patches-final-v1` | the historical wine-staging-style patch tree (153 `.patch` files + `definition`s) and the full per-patch design/measurement notes in its `README.md` |
| `tools/series.map` | maps each series to the commits on `ultimate` that produce it |
| `tools/export-series.sh` | regenerates `patches/<series>/NNNN-*.patch`, `definition`, and README bullets |

## Get the source at canonical state

```bash
git clone -b ultimate --reference-if-able /path/to/local/wine git@github.com:ZhymabekRoman/wine-ultimate.git wine-ultimate
```

Plain clone (no local reference):

```bash
git clone -b ultimate git@github.com:ZhymabekRoman/wine-ultimate.git
```

## Regenerate the patch series

```bash
tools/export-series.sh /path/to/wine-ultimate-checkout ultimate ./patches-out
```

Writes `patches-out/<series>/NNNN-<slug>.patch` plus a wine-staging style `definition`
per series (`Fixes:` header, optional `#Depends:`, file list) and
`patches-out/README.series.md`. Selection is driven by the `Series:` / `Patch-File:`
trailers in the commit messages, so the mapping is reproducible. The generated series
round-trips: applying all of it onto the root commit reproduces the canonical tree
byte-for-byte.

## Build

```bash
cd wine-ultimate
mkdir build && cd build
../configure --prefix=/usr/local/wine-ultimate --enable-archs=i386,x86_64
make -j"$(nproc)"
```

The tree is an ordinary Wine tree (`configure`, `dlls/`, `include/`, `server/`, …);
no `patchinstall.py` step is needed — the series is already in the history.
Wine-staging's own `staging/` tooling is not part of the fork.

Build *from* the tip tree: a build tree whose sources lag the tip (checked with
`diff -rq <tip> <build-source>`, sources only) produces binaries that fail the
CoreUICore connect (see below) — always rebuild after syncing the tree.

## Environment preparation (registry configuration, no binary patching)

Running a WinAppSDK/XAML-packaged app (e.g. WhatsApp Desktop 2.2620) on this fork
needs the following *registry + environment* configuration in the test prefix.
All of it is supported Wine-side behavior — no app DLLs are patched.

### 1. Build-number knob (CoreMessagingXP wake-path selection)

`CoreMessagingXP!Win32EventLoopBridge::InitializeClass` sets
`s_isSharedUserTebSupported = (PEB->OSBuildNumber >= 25197)`. Wine's default
prefix reports `19045`, which selects the legacy `OffThreadWaiter` wake path
(a waiter thread parked in `NtRemoveIoCompletionEx` on the shared IOCP). Raising
the reported build selects the modern IOCP / shared-user-TEB wake path. This is
pure registry configuration — ntdll `version_init` reads these values into the PEB:

```reg
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion]
"CurrentBuild"="26100"
"CurrentBuildNumber"="26100"
"CurrentMajorVersionNumber"=dword:0000000a
"CurrentMinorVersionNumber"=dword:00000000
"UBR"=dword:00000001
```

`import` with `wine regedit /S`. Measured A/B (150 s runs, unpatched app DLLs):
the knob flips the wake path (legacy `OffThreadWaiter` thread goes silent, the
registrar's bare ALPC receive arms and completes) but does not by itself change
the end state — see the A/B notes in the workbench harness (`wa_ver.sh set|clear`).

### 2. ActivatableClassId registration for the WindowsAppRuntime framework

`combase!RoGetActivationFactory` resolves a WinRT class through the activation
context, a few hardcoded prefixes, and then the registry fallback
`HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\<class>\DllPath`
(`REG_SZ`, backslashes doubled for regedit's parser). The framework ships class
metadata in `.winmd` files; enumerate them (e.g. `monodis --typedef`) and
register each class against the module that actually implements it. The
implementation module is a property of the binaries, not of the winmd: each
native module answers `DllGetActivationFactory` only for the classes in its
WRL creator map, so derive the assignment from the DLLs themselves (the
harness generator `~/wa-work/tools/mk_activatable_reg.py` parses the exact
650-entry creator map of `Microsoft.ui.xaml.dll` from the PE and scans the
UTF-16 class names embedded in every other module):

- `Microsoft.UI.Xaml.*` (650 classes, exact map) → `Microsoft.ui.xaml.dll`
- the WinUI "Controls" set → `Microsoft.UI.Xaml.Controls.dll`: the 151 class ids
  that module's exported `DllGetActivationFactory` answers with `S_OK` —
  `Microsoft.UI.Xaml.Controls.*`, `…Controls.Primitives.*`, `…Automation.Peers.*`,
  `…Controls.AnimatedVisuals.*`, `XamlControlsResources`,
  `XamlControlsXamlMetaDataProvider` and the WinUI media types
  (`Microsoft.UI.Xaml.Media.AcrylicBrush` and 3 more). The XAML binary does not
  implement these: its creator map and Controls' activation dispatch are disjoint
  (probed, zero overlap) and `Microsoft.ui.xaml.dll` answers them with
  `0x80040111`. Controls' compiled dispatch chain also *references* class ids it
  delegates to the XAML binary (`Microsoft.UI.Xaml.Application`,
  `Microsoft.UI.Dispatching.DispatcherQueue`, `Microsoft.UI.Xaml.Window`,
  `Windows.Foundation.Uri`, `Windows.Globalization.ApplicationLanguages`,
  `Windows.ApplicationModel.DesignMode`, the `Microsoft.UI.Private.Controls.*`
  test hooks) — mapping those to Controls costs `0x80040111` on the startup path.
  `mk_activatable_reg.py` separates them: the answer set is the probe-derived
  data file `tools/data/controls_classes.probed.txt` (pinned to the binary
  sha256 `f87bcb9b…`), the chain literals are read from the PE only within 2
  call edges of the export and serve as the delegation report
- `Microsoft.UI.Dispatching.*` → `CoreMessagingXP.dll` (the XAML binary's map
  does *not* contain the DispatcherQueue classes; pointing them at
  `Microsoft.ui.xaml.dll` yields `0x80040111 CLASS_E_NOTSUPPORTED` at
  `RoGetActivationFactory` and a `combase` fail-fast)
- `Microsoft.UI.Windowing.*` → `Microsoft.UI.Windowing.dll`
  (`InputNonClientPointerSource` → `Microsoft.UI.Windowing.Core.dll`)
- `Microsoft.UI.Input.*` / `Microsoft.UI.Content.*` → `Microsoft.UI.Input.dll`
- `Microsoft.UI.Composition.*` → `dcompi.dll`
  (`…Composition.SystemBackdrops.*` → `wuceffectsi.dll`)
- `Microsoft.Windows.ApplicationModel.Resources.*`, `Microsoft.Windows.Globalization.*`
  → `Microsoft.Windows.ApplicationModel.Resources.dll`
- `Microsoft.Windows.Widgets.*` → `Microsoft.Windows.Widgets.dll`
- remaining `Microsoft.Windows.*` (AppLifecycle, DynamicDependency, Deployment,
  AppNotifications, PushNotifications, Storage, System, AccessControl) →
  `Microsoft.WindowsAppRuntime.dll`

System-provided classes resolve to Wine builtins:
`Windows.Foundation.Metadata.ApiInformation` (plus `PropertyValue`,
`PropertySet`, `Streams.Buffer`, `Streams.DataWriter`) → `wintypes.dll`;
`Windows.UI.ViewManagement.{UISettings,UIViewSettings,AccessibilitySettings,InputPane}`
and `Windows.UI.Core.{CoreWindow,CoreCursor}` → `windows.ui.dll`. These are on
the XAML startup path: `Microsoft.Internal.FrameworkUdk.dll!Switcher::GetOSVersion`
fail-fasts with `0x80040154` if `ApiInformation` does not activate, and
`FrameworkApplication::StartDesktop` fails on `UISettings`. The remaining
background classes the app activates during startup, each probe-verified against
the module's `DllGetActivationFactory` (a module answers only the ids in its own
dispatch — `Windows.ApplicationModel.Core.CoreApplication` is answered by
`twinapi.appcore.dll`, **not** by `windows.applicationmodel.dll`; the
AnalyticsInfo class is `Windows.System.Profile.AnalyticsInfo`, answered by
`twinapi.appcore.dll`):

```reg
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.System.Profile.AnalyticsInfo]
"DllPath"="C:\windows\system32\twinapi.appcore.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Media.Devices.MediaDevice]
"DllPath"="C:\windows\system32\windows.media.devices.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Devices.Enumeration.DeviceInformation]
"DllPath"="C:\windows\system32\windows.devices.enumeration.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Networking.Connectivity.NetworkInformation]
"DllPath"="C:\windows\system32\windows.networking.connectivity.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.ApplicationModel.Core.CoreApplication]
"DllPath"="C:\windows\system32\twinapi.appcore.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.ApplicationModel.DesignMode]
"DllPath"="C:\windows\system32\windows.applicationmodel.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Globalization.ApplicationLanguages]
"DllPath"="C:\windows\system32\windows.globalization.dll"
[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\Windows.Foundation.Uri]
"DllPath"="C:\windows\system32\wintypes.dll"
```

Keys left over from an earlier, wider mapping are **not** overwritten by a
narrower import — `combase` keeps resolving the stale `DllPath` (a leftover
`Windows.ApplicationModel.Core.CoreApplication → Microsoft.UI.Xaml.Controls.dll`
cost a startup failure after the Controls mapping was corrected). The generator
emits `[-HKEY_…]` deletions for every key its map no longer contains
(`/tmp/wa_activatable_del.reg`) and the harness imports them before the map.

App defaults and the build knob (per-app Windows version + the CoreMessagingXP
wake-path knob of §1):

```reg
[HKEY_CURRENT_USER\Software\Wine\AppDefaults\WhatsApp.Root.exe]
"Version"="win11"
```

### 2a. winsxs common-controls v6 assembly directory

The XAML binary delay-loads `comctl32.dll!InitCommonControlsEx` under its
embedded `Microsoft.Windows.Common-Controls` v6 manifest dependency. With the
manifest present in `windows/winsxs/manifests/` (wineboot installs it) Wine's
loader redirects the DLL into the assembly directory, so that directory has
to exist and carry the DLL, otherwise the delay-load fails and the loader
aborts the process (`unimplemented function comctl32.dll.InitCommonControlsEx`):

```
drive_c/windows/winsxs/amd64_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef/comctl32.dll
```

(copy of `system32/comctl32.dll`).

### 2b. Pristine binaries

All app and framework-package DLLs run unmodified; the three DLLs that an
earlier workbench generation carried patched (`CoreMessagingXP.dll`
`24da83be…`, `Microsoft.Internal.FrameworkUdk.dll` `c6053f1a…`,
`dwmcorei.dll` `9353066e…`) are byte-identical to their `.orig` originals.
The kernel-accurate ALPC framing commit (`fix(alpc,ntdll): kernel-accurate
message framing`) plus the registry configuration above replace the
`ValidateBufferHeader` / Abandonment patches: on pristine binaries the
`CoreMsgCreateSession` handshake's connect, join request and synchronous
reply validate (`Type=LPC_REPLY`, `TotalLength == payload + 0x50`, port
context `== NtAlpcAcceptConnectPort` context).

### 3. Package identity environment

`twinapi.appcore` / package-identity series read the package graph from the
environment (no installation is required):

```bash
export WINE_PACKAGE_FULL_NAME='5319275A.WhatsAppDesktop_2.2620.102.0_x64__cv1g1gvanyjgm'
export WINE_PACKAGE_FAMILY_NAME='5319275A.WhatsAppDesktop_cv1g1gvanyjgm'
export WINE_PACKAGE_AUMID='5319275A.WhatsAppDesktop_cv1g1gvanyjgm!App'
export WINE_PACKAGE_PATH='Z:\path\to\app\package'
export WINE_FRAMEWORK_PACKAGE_FULL_NAME='Microsoft.WindowsAppRuntime.1.6_6000.519.329.0_x64__8wekyb3d8bbwe'
export WINE_FRAMEWORK_PACKAGE_PATH='Z:\path\to\packages\Microsoft.WindowsAppRuntime.1.6_6000.519.329.0_x64__8wekyb3d8bbwe'
```

### 3a. Prefix materialization (builtin copies must equal the build tree)

Wine 11 materializes builtin PE DLLs into the prefix (`drive_c/windows/system32`,
`syswow64`) and loads builtins **from the prefix**, not from `WINEDLLPATH`. A
restored/skeleton prefix fails at `kernel32.dll` (`c0000135`); repopulate from
the build tree (`dlls/*/x86_64-windows/*.dll` → `system32`,
`dlls/*/i386-windows/*.dll` → `syswow64`) before first run.

The copies go stale silently after a source sync: measured on this prefix, 22
modules lagged the tip build (`combase`, `twinapi.appcore`, `windows.ui`,
`wintypes`, `coremessaging`, `d3d11`, `dcomp`, `dxgi`, `user32`,
`windows.system.threading`, …) — every measurement taken against them reflects
the old tree, and a fix that lands in the tree does not reach the app.
`~/wa-work/wa_syncbuiltins.sh` re-materializes every
`dlls/*/x86_64-windows/*.dll` (plus the comctl32 v6 assembly copy of §2a),
reports `new/updated/unchanged`, and is idempotent; run it after every build.

## CoreUICore self-host registrar connect (partition-connect failfast)

Symptom was a deterministic startup abort: 60 s battery, 8/8 runs `rc=2`, main thread
fail-fast at `CoreMessagingXP+0xb8fd` (`CFlat::Abandonment::FailWithException`) reached from
`Registrar::RemoteRegistrarClient::WaitForServiceStartup`.

Recovered call chain (IDA, pristine `CoreMessagingXP.dll` `24da83be…`):
`DispatcherQueueController::CreateOnCurrentThread` → `CoreUISessionCreate` →
`CoreUISession::EnsureInitializedWithAddRef` (self-host branch: `RegistrarHost::CreateSingleton(1)`
→ `CreateThread(RegistrarThreadProc)` → `CoreUIRunRegistrarServer` → `CuiRegistrar::Run`) →
`AlpcServerHost::Create$` → `AlpcServerConnection::CreateServer` → `NtAlpcCreatePort` of
`\BaseNamedObjects\CoreMessagingRegistrar-<pid>:<Guid.Empty>` → `Win32Event::Set(startup)`;
meanwhile the main thread's `RegistrarClient::ConnectToRegistrarServer` →
`RemoteRegistrarClient::Connect` → `DoConnect` → `AlpcClientAdapter::Connect`. `DoConnect` returns
the raw connect status and every status except `STATUS_ACCESS_DENIED` falls into the
`WaitForServiceStartup` fail-fast stub — so a single rejected connect kills the process.

Root cause was **not** init slowness. Measured (`+timestamp,+alpc,+loaddll,+wined3d,+d3d11`):
registrar port created at +0.37 s, D3D11 device + feature levels in ~0.2 s, no init phase
exceeds 0.5 s, and the native protocol already orders create-before-connect (the startup event
is signalled *after* `NtAlpcCreatePort` returns). It was Wine's connect-reply framing: the kernel
completes `NtAlpcConnectPort` with the server's reply message *plus* the CONTEXT attribute
(port context, connect message id) and the RESERVE (reverse) port; the pre-tip ntdll left the
wine wire header in the caller's buffer and filled neither attribute, so the client rejected its
own connection. Fixed by the tip pair `1134e8dc5` (reverse/reserve port) + `b50acba1e`
(kernel-accurate message framing). The 8/8 battery had been measuring a build tree whose
`dlls/ntdll/unix/alpc.c` predated those commits.

**Keep the build tree's source at the canonical tip.** A tree that predates `b50acba1e`
reproduces the 8/8 fail-fast; verify with `sha256sum` that app binaries are the pristine
`24da83be…` / `9a42e19d…` and rebuild after syncing.

Measured after the sync (probe-free build): battery 8×60 → 8/8 `rc=124 ALIVE`, `fatal=0`;
130 s and 150 s runs alive, 20 windows created (`NtUserCreateWindowEx`), 25 X windows
(25 × `Default IME` 1×1 + one `.NET-BroadcastEventWindow`), `coreclr`/`System.*` payload loads,
`d3d11_device_context_DrawIndexed` renders. Next gaps: no visible XAML window (no
`WindowsXamlManager` activation attempt, root window blank), `WebView2Loader` /
`msedgewebview2` counts 0, and one pre-sync run showed a 60 s `sync_cs` stall in the
`ntoskrnl.exe` builtin (`blocked by` another thread) worth its own look.

## CoreUICore dispatcher conversation (join contract, ALPC handle attributes)

Tip pair `2eda257cc` + `ea645a491` (ultimate, linear after `62b11019d`).

**Kernel gap closed (`2eda257cc`)**: the ALPC `LPC_HANDLE_ATTRIBUTE` path. A sender
attaching `ALPC_MESSAGE_HANDLE_ATTRIBUTE` hands the kernel one object handle; the kernel
duplicates it into the receiving process's handle table on delivery, reports it in the
receiver's handle attribute with the notification action (1=created, 2=closed, 3=modified,
4=closed-in-closing-process) and the handle count, and closes the duplicate when the
message is dropped. Server: per-port owning process, duplication into the receiver at
queue time with bookkeeping on the queued message (teardown closes = the close-notify
path; delivery transfers ownership), carried as an optional block between the wine wire
header and the user payload — the connect handshake replies land in caller buffers sized
for the plain 72-byte header, so the header itself must not grow (an enlarged header
stack-smashes the app's 64-byte connect buffer: the app hangs with a silent wineserver
crash if the queued-message bookkeeping fields are not zeroed). ntdll marshals both
directions. No protocol.def change.

**Self-host conversation model (`ea645a491`)**, measured with wire probes on pristine
binaries: the XAML engine registers the `CoreUICore` conversation itself (registrar
method 14 on its self-host connection) and serves dispatcher-queue joins in-process.
The builtin provider must NOT register that name — its registration answered result 4
(already-registered) to the engine's own registration. The provider registers its thread
only; routed joins (17/18) are answered with the native reply layout recovered from
`RegistrarServerCaller::RegisterConversationClient` → `CoreUICallSend`:
`[result, clientId, ConversationInfo(16), {serverId u64,u32}, peers(56-byte
ConversationPeerInfo each), peerCount]` plus the ExportEndpoint notification handle on
the reply's ALPC handle attribute. Unmarshal reference:
`Thunk_Reply_RegisterConversationClient_73` (peers slot 4, count slot 10).

**Failfast site identified (gap 4)**: `Microsoft.ui.xaml.dll+0x7E4D83` is
`DirectUI::ErrorHelper::OriginateError`; the 45 startup occurrences chain through
`CErrorService::ReportParserError → CErrorService::AddError →
CJupiterErrorServiceListener::NotifyErrorAdded` (stowed bt, base-corrected): XAML's
binary object writer fails to instantiate types whose WinRT activation answers
E_NOINTERFACE — observed: `Microsoft.UI.Xaml.XamlTypeInfo.XamlControlsXamlMetaDataProvider`
(combase checks `Microsoft.ui.xaml.dll`; on Windows this class lives in
`Microsoft.UI.Xaml.Controls.dll` — env-prep registration mapping gap) and
`Microsoft.UI.Xaml.Media.AcrylicBrush` (module correct, XAML's `DllGetActivationFactory`
refuses on Wine). These are XAML parser/activation failures, not conversation-client
failures: the dispatcher join is no longer the blocker after `ea645a491`. Next: correct
class→module activation registration for XAML Controls classes and trace why
`Microsoft.ui.xaml.dll`'s activation factory rejects `Microsoft.UI.Xaml.*` under the
Wine self-host (likely CoreWindow/desktop-surface init state).

## XAML activation layer: cleared (Controls mapping, d2d1 published device ids)

The fail-fast site of the previous section is resolved. The `E_NOINTERFACE`
activations that fed `CErrorService::ReportParserError` → managed
`RaiseFailFast` (`c0000602`, `rc=2`) had three distinct causes, **none of them a
CoreWindow/desktop-surface or `WindowsXamlManager` init gate**:

1. **Module mapping (registry configuration).** `XamlControlsResources`,
   `XamlControlsXamlMetaDataProvider`, `Microsoft.UI.Xaml.Media.AcrylicBrush`
   and the rest of the WinUI Controls set are implemented by
   `Microsoft.UI.Xaml.Controls.dll`. `AcrylicBrush` is *not* in
   `Microsoft.ui.xaml.dll`'s 650-entry creator map (parsed from the PE), which is
   why that module answered `0x80040111` for it: the refusal was a mapping gap,
   not a runtime gate. DGAF matrix over all 3879 `Microsoft.UI.*` ids:
   `Microsoft.ui.xaml.dll` answers 627, Controls 151, **zero overlap**.
2. **Stale and over-broad registry keys.** Mapping every
   `Microsoft.UI.Xaml.*` id that is not in the XAML map to `Microsoft.ui.xaml.dll`
   (the old namespace-prefix fallback) and harvesting Controls' dispatch-chain
   literals both produced keys for ids the module delegates
   (`Microsoft.UI.Xaml.Application`, `Microsoft.UI.Dispatching.DispatcherQueue`,
   `Windows.Foundation.Uri`, …); the app asked those and got `0x80040111`. See
   §2/§2b for the corrected derivation and for deleting superseded keys.
3. **`d2d1` published device interface ids (source gap).** `dwmcorei.dll`'s
   `DllMain` → `Startup()` calls `D2D1CreateFactory(ID2D1Factory1)` and queries
   the factory for `ID2D1Device` (`{6f72c0a2-6db7-46e9-9b62-b58a23f4928b}`).
   Wine's d2d1 uses locally generated ids for the device family
   (`ID2D1Device` = `47dd575d-…` in `include/d2d1_1.idl`) and the factory object
   refused device queries outright, so the query returned `E_NOINTERFACE`;
   `Startup` failed, `DllMain` returned `FALSE`, `dwmcorei` failed its image load
   with `ERROR_DLL_INIT_FAILED` (`0x45a`), and because `dcompi.dll` imports
   `dwmcorei` its load failed too — which fails **every**
   `Microsoft.UI.Composition.*` activation (the app's first composition
   activation is what raised the managed fail-fast). Fixed in `ultimate` by
   `d2d1: answer the published device interface ids on the factory`
   (`eb74e0827`): the factory answers the published ids of `ID2D1Device`/
   `ID2D1Device1` with its implicit device (built on the factory's D3D10 device,
   as `CreateDevice()` does) and the device/context objects accept the published
   ids as aliases.

Measured after the fix (pristine app binaries, hashes identical to `.orig`,
registry configuration only, probes removed):

| metric | before | after |
| --- | --- | --- |
| `RoGetActivationFactory … not found in` (parser chain) | 45–48 | **0** |
| `RaiseFailFastException` | 32 | **0** |
| 150 s run | `rc=2` DIED | **`rc=124` ALIVE**, `fatal=0`, `av=0` |
| battery 8 × 60 s | 8/8 `rc=2` DIED | **8/8 `rc=124` ALIVE**, `fatal=0` |
| windows created (`NtUserCreateWindowEx`) | ~10 | **19** |
| `WebView2Loader` / `msedgewebview2` | 0 / 0 | 0 / 0 |
| `WinUIDesktopWin32WindowClass` X windows | 0 | **0** (screenshot blank, 295 B) |

The XAML-activation fail-fast is gone and the process survives the whole
startup sequence, but the app is **parked before its XAML window**: no
`WinUIDesktopWin32WindowClass` registration, no visible window, no WebView2
boot. Next gaps: (a) find where the main thread parks after the activation
layer resolves — the thread-dump step of `wa_diag150.sh` is broken (its
`winedbg` pid detection attaches a stale pid, "Can't attach process 0020:
error 5") and must be repaired before the parked stack can be read;
(b) `ID2D1Device2…6` published ids are still unmapped in `d2d1`;
(c) the DComp device path (`dcomp`/`dwmcorei` compositor creation) has not been
exercised yet, since the app never reaches composition.

## The park after XAML activation: loader_section / wined3d_cs AB-BA cycle

Measured at tip `eb74e0827` with the repaired diagnostics (pristine app binaries, hashes
verified against `.orig`, registry configuration only). Reproduced in 4/5 runs, stable
thread signature, 3 repeats of every 60 s timeout per run.

**The app is not waiting on a conversation, an async, an activation, or composition.**
The main thread is deadlocked with a render thread:

```
0024:err:sync:RtlpWaitForCriticalSection section .../wined3d_main.c: wined3d_cs
        wait timed out in thread 0024, blocked by 0148, retrying (60 sec)
0148:err:sync:RtlpWaitForCriticalSection section .../ntdll/loader.c: loader_section
        wait timed out in thread 0148, blocked by 0024, retrying (60 sec)
```
plus 8 further threads (`00f8 0158 0160 0164 0168 0174 0188 018c 01a0`) queued behind
`loader_section`. Cycle edges, from the `+module` trace at the moment of the stall:

1. **main thread 0024 owns `loader_section`**, is inside
   `process_attach (L"dwmcorei.dll") - START` with no matching END (dwmcorei is loaded as
   an import of `dcompi.dll`, so its `DllMain` runs under the loader lock), and is asking
   for `wined3d_cs`. That edge is the tip commit `eb74e0827`: answering
   `ID2D1Device`/`ID2D1Device1` on the d2d1 factory builds the implicit device inside
   `d2d_factory_QueryInterface` — i.e. inside dwmcorei's `DllMain` — and device creation
   enters d3d10/wined3d, which takes `wined3d_cs`. Before the tip, dwmcorei's `DllMain`
   failed earlier, so the cycle did not exist.
2. **thread 0148 owns `wined3d_cs`** (it is a render thread: it created the XAML windows
   `0x1006c`/`0x10064`, and the llvmpipe worker pool hangs off it), and its last action is
   `load_dll looking for L"opengl32.dll"` → `LdrLoadDll` → `loader_section`. The site is
   the lazy forwarding in `dlls/gdi32/opengl.c`: `ChoosePixelFormat`/`DescribePixelFormat`
   /`GetPixelFormat`/`SetPixelFormat`/`SwapBuffers` do
   `if (!opengl32) opengl32 = LoadLibraryW( L"opengl32.dll" )`, and wined3d's GL init
   calls `ChoosePixelFormat` while holding `wined3d_cs`. Preloading opengl32 does not
   help: `LoadLibraryW` on an already-loaded module still takes the loader lock
   (`LdrGetDllHandleEx L"opengl32.dll" -> 0x7BC10000` shows it was already mapped).

Fix direction (Wine-side, no app patching): remove edge 1 — the d2d1 factory must not
realize the implicit device during a `DllMain`. Answer the published-id query with a
forwarder that creates the real device at first use (outside the loader lock), keeping the
tip's published-id aliases. Edge 2 is the same class of bug as the upstream rule "a client
lock must never be held across `LdrLoadDll`"; resolving gdi32's opengl32 forwarding at
gdi32 init, not per call, closes it as well.

### Process addressing (what the harness had wrong)

* The app's **Linux** process has comm `UI thread` (its `SetThreadDescription` on the main
  thread is propagated by Wine into `comm`), so `pgrep -x WhatsApp.Root.e` /
  `pkill -x WhatsApp.Root.e` match nothing; select it from `/proc/<pid>/cmdline`.
* The **Wine pid and every Wine tid** come out of the app's own TEBs: Wine on x86-64 keeps
  the TEB in the GS segment base and `TEB+0x40/+0x48` is
  `ClientId { UniqueProcess, UniqueThread }` — one gdb batch pass over all threads yields
  the pid (modal non-zero value; host-injected threads, e.g. mangohud, report 0) and the
  LWP→tid map. Non-Wine threads (llvmpipe workers) inherit the creator's TEB, which is how
  the llvmpipe pool was traced back to `0148`.
* gdb cannot unwind Wine: Wine's PE/ELF hybrid DLLs expose no CFI that BFD reads, so
  `thread apply all bt` degenerates to bare addresses; and winedbg's attach path
  (`DebugActiveProcess`) returns a stale thread list, labels the attached process
  `C:\windows\system32\start.exe`, and mislabels the `0x140000000` module — its
  symbolization of an attached app is unusable. Wine 11 also maps builtin code from an
  anonymous `memfd:wine-mapping`, so `/proc/<pid>/maps` cannot name builtin modules; the
  module bases come from the `+loaddll` trace.
* The coreclr diagnostics IPC (the `dotnet-dump` attach path) is unreachable: wineserver
  has no AF_UNIX transport for app sockets (`server/sock.c` uses AF_UNIX only for its own
  `socketpair`), so the runtime's diagnostic listener never binds. `winedbg --command
  "minidump <path>"` does write a minidump of an attached process.

Harness: `~/wa-work/wa_diag150.sh` (repaired: TEB addressing, per-snapshot tid map, raw
stack capture, minidump, window census, app state-machine tail),
`~/wa-work/tools/btsymbol.py` (raw stack words + `+loaddll` bases + `nm` VAs → module and
nearest function; `nm` addresses match the runtime VAs because Wine maps DLLs at their
preferred bases), `~/wa-work/wa_lockprobe.sh` (the `+module` probe that pins the two
in-flight `LdrLoadDll` calls), `~/wa-work/wa_pidprobe.sh`, `~/wa-work/wa_gdbprobe.sh`.
The `ERR("created window %p")` trace in `win32u/window.c:5995` is leftover probe-era
instrumentation and should go.

## Series on `ultimate`

| Series | Patches | Notes |
| --- | --- | --- |
| `dcomp-DCompositionCreateDevice2` | 4 | `0001` = squashed vendor import of the CodeWeavers/Zhiyi Zhang series 0001–0066 (Wine MR !2243), then per-commit 0067–0077 |
| `ntdll-NtAlpcCreatePort` | 1 | ALPC port infrastructure |
| `ntdll-WaitCompletionPacket` | 11 | wait completion packets + ALPC integration |
| `kernelbase-PackageIdentity` | 5 | package identity, windowing model, handle rights |
| `twinapi-appcore-UWP` | 31 | ICoreApplication/CoreWindow/DispatcherQueue, WinRT surfaces, Windows.UI.Composition, ALPC tests |
| `combase-AgileObject` | 1 | IAgileObject on the FT marshaler |
| `coremessaging-DispatcherQueue` | 1 | IDispatcherQueue + CoreUICreateDispatcherQueueController |
| `profapi-UserToken` | 1 | ordinal 114 stub |
| `windows.graphics-DisplayInformation` | 1 | IDisplayInformation |

56 patches total, on top of 23 `wip:` commits that carry the remaining development-tree
state (dcomp transforms, `windows.ui.composition`, `frameworkudk.system`, dxgi/d3d11/wined3d,
server ALPC/d3dkmt, ntdll WNF/ALPC, twinapi, windows.ui, cryptowinrt, winrt stubs, build glue).

## Migration note

* Upstream base: Wine **11.10**, upstream commit
  `2cac6ccf33c0807f374dc96f5a20e35a2da86157` ("Release 11.10.").
* Fork root: `558b92069ffa44508170e8622eb5471b0000faf8` — that snapshot as a
  parentless root commit (the upstream history below 11.10 is intentionally not carried;
  the clone is self-contained).
* Canonical tip: `1f8843e2afb32868c01eed50de6440a6e9e47823` = tag `series/v0052-dcomp0077`.
* Archive of the previous patch-tree layout: branch `patches-archive`, tag
  `patches-final-v1` (tip of the old `main`, 153 patch files, and the long per-patch
  measurement/A-B notes — read them there).
* Applied history: 153 archived patch files → 119 applied as commits, 32 superseded by
  later dev-tree state and captured in the `wip:` commits, 66 CodeWeavers patches squashed
  into one vendor commit. The per-patch am log (applied / skipped / failed with the
  strategy that worked) is in the migration workbench at
  `~/migration-tools/am-log2.tsv`; it is not tracked here.

## Remotes

* `github` → `git@github.com:ZhymabekRoman/wine-ultimate.git`
* Upstream Wine: `https://gitlab.winehq.org/wine/wine.git`
* Wine-Staging: `https://gitlab.winehq.org/wine/wine-staging.git`

## The park fix: d2d1 implicit-device forwarder + gdi32 init-time opengl32 forwarding

Implemented on ultimate as two commits (pushed, linear from eb74e0827):

- `d2d1: realize the factory's implicit device lazily behind a forwarder` — the
  factory answers ID2D1Device/ID2D1Device1 (+ published Microsoft ids) with a
  lightweight forwarder (ID2D1Device/ID2D1Device1 vtable shim). The real device is
  created at the first method call, outside any loader initialisation; the forwarder
  interface pointers are the stable client-visible COM identity, the realized device
  is internal (only answers iids the shim does not implement, on demand). The
  forwarder holds a factory ref for its life; the realized device takes its own, as
  d2d_device_init does. D3D10 device binding is the CreateDevice() one.
- `gdi32: resolve the opengl32 forwarding at process init` — gdi32 DllMain loads
  opengl32 and caches wglChoosePixelFormat/wglDescribePixelFormat/wglGetPixelFormat/
  wglSetPixelFormat/wglSwapBuffers; the per-call LoadLibraryW (loader lock under a
  wined3d_cs holder) is gone. Module-absent semantics preserved (functions fail as
  before).

Verification (tip 6d9288cef, prefix builtins synced):

- Lock probe (100 s, +module,+loaddll): **0 lock timeouts** (was 27/run), no
  loader_section/wined3d_cs cycle signature; the main thread progresses past the old
  park into uiautomationcore/oleacc/dataexchange (rc=0 at 100 s, was 124-parked).
- 150 s run: app now dies on a **managed fail-fast c0000602** —
  win32u/user32 window-proc dispatch → Microsoft.ui.xaml (+0x213c35…+0x699fe) →
  combase+0x19114 → kernelbase RaiseFailFast. 30 windows created (incl. a
  1280x1024), **WinUIDesktopWin32WindowClass not registered**, no mapped XAML
  window; screenshot `wa-work/wa_deadlock_after.png` is blank. The app dies before
  XAML window registration.
- Battery 8x60: 8/8 rc=2 DIED, fatal=0, webview2loader=0, edge=0 (deterministic).
- dwmcorei loads (no 0x45a), dcompi loads, Microsoft.UI.Composition.OSSupport maps —
  the composition path from eb74e0827 survives the laziness. App binaries pristine
  at end: CoreMessagingXP 24da83be…, FrameworkUdk c6053f1a…, Microsoft.ui.xaml
  9a42e19d…, dwmcorei 9353066e… (== .orig set intact).
- Tests (gdi32, dcomp, d2d1, dxgi) running in background; monitor:
  `tail -f ~/wa-work/wa_tests_after.log`.

Next gap: the combase activation fail-fast inside the XAML window proc — a class
server activation failing during XAML content init (the recorded ActivatableClassId
module-mapping family). webview2/edge is not reached.

## The XAML window-proc activation fail-fast: cleared (ActivatableClassId module evidence)

Tip `6d9288cef` died deterministically in 8/8 runs: `win32u/user32` window-proc dispatch →
`Microsoft.ui.xaml.dll+0x213c35 → +0x291a0d → +0x28fe1a → +0x699fe → +0x695df/54d/4a4`
(the low-RVA group is the window-proc/input-source region) → `combase+0x19114`
(`RoFailFastWithErrorContextInternal2`) → `kernelbase!RaiseFailFastException`,
`c0000602`, `rc=2`.

**The failing activation, measured with a temporary combase probe (`roapi.c`
`RoFailFastWithErrorContextInternal2`/`RoOriginateLanguageException` summary at ERR, removed
before commit — the source tree is clean):**

```
RoGetActivationFactory (L"Microsoft.UI.Input.InputNonClientPointerSource",
                        {7d0b775c-1903-5dc7-bd2f-7a4b31f0cff2}, …)
  → DllPath Z:\…\5319275A…\Microsoft.UI.Windowing.Core.dll
  → Class … not found in …Microsoft.UI.Windowing.Core.dll, hr 0x80004002 (E_NOINTERFACE)
RoFailFastWithErrorContextInternal2  error 0x80004002, exception_count 1
```

The XAML window proc is in the content-island/input-source sequence — the activations
around it resolve to `Microsoft.UI.Input.dll` (`ContentIsland`, `InputSiteBridge`,
`InputFocusController`, `InputKeyboardSource`, `InputPreTranslateKeyboardSource`,
`InputPointerSource`, `InputActivationListener`, `Content.DesktopChildSiteBridge`) and one
id is pinned to `Microsoft.UI.Windowing.Core.dll` by the recorded env-prep map.

**Cause: registry configuration, not a Wine gate.** `Microsoft.UI.Windowing.Core.dll` does
not contain the class in its activation table: its image holds the class-id literal with
**zero** absolute 8-byte pointer xrefs (a string constant), while
`Microsoft.UI.Input.dll` holds it with one (its ClassInfo entry, stride 32
`{name, interfaces, iids, count=2}`) and carries the activation-factory IID
`7d0b775c-…` once. So the module answered `E_NOINTERFACE` for a class it was never the
implementation of.

**Derivation added to the env-prep generator** (`~/wa-work/tools/mk_activatable_reg.py`),
replacing namespace-prefix guessing for the WinAppSDK modules:

1. *Static activation-table index* — the C++/WinRT ClassInfo tables and the WRL creator map
   are pointer tables, so a class id they contain is referenced by an absolute 8-byte
   pointer inside the module's own `.rdata/.data`. 1030 ids over 13 modules; reproduces
   **650/650** of the known `Microsoft.ui.xaml.dll` WRL map (positive control) and gives
   `InputNonClientPointerSource → Microsoft.UI.Input.dll`,
   `Content.ContentIsland → Microsoft.UI.Input.dll`,
   `Composition.Compositor → dcompi.dll` (the working mapping), uniquely.
   `Microsoft.UI.Xaml.Controls.dll` and `CoreMessagingXP.dll`/`Microsoft.UI.Windowing.dll`
   use compiled dispatch (no pointer table) → keep the probed set / prefix rules.
2. *Activation-factory IID multiplicity* — the IID the app asks for (logged by combase's
   `RoGetActivationFactory` FIXME) occurs **twice** in the implementing module's interface
   table and once in each consumer: `Microsoft.UI.Colors` `{8620a5b0-…}` →
   `Microsoft.UI.dll` x2 (Controls/xaml x1, and Controls answered 0x80040111 in-app).
3. *Missing builtin keys* — keys an older generation had emitted for system-provided
   classes: `Windows.Storage.ApplicationData → windows.storage.applicationdata.dll`,
   `Windows.ApplicationModel.Package → windows.applicationmodel.dll`,
   `Windows.Globalization.GeographicRegion/Language → windows.globalization.dll`,
   `…DragDrop.Core.CoreDragDropManager → dataexchange.dll`.

Live DGAF matrix probing of these modules outside the app is **not usable**: loading
`Microsoft.UI.Input.dll`/`Microsoft.UI.dll` standalone faults inside
`DllGetActivationFactory` (`Microsoft.UI.Input.dll+0x11566`, `movq %rbx,(%r8)`) and
`CoInitializeEx(MTA)` faults in a bare probe. Probes kept in `~/wa-work/probes/`
(`wrl_map_scan.py`, `literal_xref_scan.py`, `activation_name_arrays.py`, `guid_scan.py`,
`probe3.c`, `probe4.c`).

**Stale-key deletion is a no-op and must stay that way until the derivation is complete.**
The generator's deletion pattern matches `]\t` while a real `system.reg` line is
`] <hextime>`, so deletions never fire (the README's §2b "emits `[-HKEY_…]` deletions" is
aspirational). Measured with the pattern corrected: 209 prefix keys are not emitted by the
current map; deleting them costs one class — `Windows.Storage.ApplicationData` — and the
whole window-proc fail-fast returns (9 `RoFailFast*`, 15 windows, `rc=2`). The prefix
therefore carries the union of generations; the derivation under-emits ~209 ids.

Measured after the mapping fix (pristine app binaries, registry configuration only, probes
removed from the Wine tree, no source change):

| metric | tip `6d9288cef` | after |
| --- | --- | --- |
| `c0000602` / `RoFailFastWithErrorContextInternal2` | 8/8 runs, 1 per run | **0 / 0** |
| app-managed fastfail `c0000409` param `0x80040154` | – | **0** |
| battery 8 × 60 s | 8/8 `rc=2` DIED (fail-fast) | **8/8 `rc=69`**, `fatal=0`, `unhandled=1` (CLR `e0464645`) |
| 150 s run | `rc=2` | `rc=69` (one run reached `rc=124` ALIVE) |
| windows created | 30 | **35** (172 X windows) |
| unresolved activations (`Failed to find library`) | – | **2**: `FakeClass.ClassName` (app's own negative probe), `Microsoft.UI.Text.FontWeights` |
| lock timeouts / AV | 0 / 0 | 0 / 0 |
| `WinUIDesktopWin32WindowClass` | 0 | **0** (no mapped XAML window; `wa-work/wa_combase_after.png` blank, 295 B) |
| `WebView2Loader` / `msedgewebview2` | 0 / 0 | 0 / 0 |
| app binaries | pristine | pristine (24da83be…, c6053f1a…, 9a42e19d…, 9353066e… == `.orig`) |
| tests | – | combase 0 fail, d2d1 0 fail, gdi32 = baseline `dc.c GetDeviceGammaRamp`, dcomp fails (pre-existing at `d6de043f1`) |

Next gaps: (a) **`Microsoft.UI.Text.FontWeights` is answered by nobody** —
`Microsoft.ui.xaml.dll`, `Microsoft.UI.dll` and `Microsoft.UI.Xaml.Controls.dll` all return
`CLASS_E_CLASSNOTAVAILABLE` in-app, although the id is in the XAML 650-entry map and the
IID `cc390df6-…` occurs in `Microsoft.ui.xaml.dll`; the XAML parser then reports
`XAML parsing failed.` (`0x802b000a`) and the app dies in its own managed path
(`HybridDialogUnexpectedErrorOccurred`, `e0464645`, `rc=69`). The fix is a Wine-side
provider for the class (factory answering `IActivationFactory` + the activatable IID
`cc390df6-76b0-5807-8b9d-e949a4e623ae`, statics `Black…ExtraBlack` returning `FontWeight`)
mapped to that builtin. (b) The ~209 under-emitted ids (start with the class-id literals of
the framework module images, not only pointer-table ids). (c) `WinUIDesktopWin32WindowClass`
registration, webview2/edge: not reached.


## The XAML parse's last activation gap: FontWeights provider, complete-id emission, tray path

Tip `6d9288cef` (fail-fast 0, battery 8/8 `rc=69`): the XAML parser activates
`Microsoft.UI.Text.FontWeights` (activatable IID `cc390df6-76b0-5807-8b9d-e949a4e623ae`);
the key is absent, the in-process namespace-prefix dll probe fails, combase returns
`REGDB_E_CLASSNOTREG` → `RoOriginateLanguageException 0x80040111` → the parse reports
`XAML parsing failed.` (`0x802b000a`) → the app's `HybridDialogUnexpectedErrorOccurred`
throws CLR `e0464645` → `rc=69`. In-app probe matrix: `Microsoft.ui.xaml.dll`,
`Microsoft.UI.dll`, `Microsoft.UI.Xaml.Controls.dll` all answer `CLASS_E_CLASSNOTAVAILABLE`.

**RE (static, pristine binaries):** the class id is in **no** activation table — not the
650-entry WRL creator map (650 ids enumerated, `Microsoft.UI.Text.*` absent), not the
C++/WinRT ClassInfo pointer-xref set (1030 ids over 13 modules), not the `DllGetActivationFactory`
dispatch-chain literals. The IID blob sits at `Microsoft.ui.xaml.dll+0xae4b98` (.rdata, no
absolute pointer xrefs) with three RIP-relative `lea` **code** xrefs: the parser's *internal*
consumer sites (`+0x8e9d9e` builds the activation descriptor from the class-name literal at
`+0x1cb1980`, activates, then QIs the object for `cc390df6-…`). So the module implements the
statics for its own parse use and refuses the class externally, permanently (its activation
dispatch is table-driven). A Wine builtin provider is required; the winmd
(`Microsoft.UI.Text.winmd`, `ikdasm`-decoded) fixes the ABI: `IFontWeightsStatics` =
`cc390df6-…` with 11 propget methods in metadata order (Black, Bold, ExtraBlack, ExtraBold,
ExtraLight, Light, Medium, Normal, SemiBold, SemiLight, Thin), the empty default
`IFontWeights` = `386cd040-…`, the return struct is the UWP `Windows.UI.Text.FontWeight`
(`{UINT16 Value}`, contract-gated).

**Provider (source, `ultimate`):** `windows.ui.dll` answers the class (factory QIs
`IActivationFactory`/`IFontWeights`/`IFontWeightsStatics`, 11 getters return the documented
WinUI weights, `E_NOINTERFACE` for the rest, `IAgileObject` answered) + the boxed value type
for the parameterized activatable id ``Windows.Foundation.IReference`1<Windows.UI.Text.FontWeight>``
(closed pinterface IID `fd27fa16-d457-5871-86d0-58728fb611af`). `struct FontWeight` added to
`include/windows.ui.text.idl` (widl resolves cross-file interface params only for typedef'd
structs; the `declare` of the pinterface itself is rejected for `.idl`-struct params —
widl instantiates pinterface params from winmd metadata only). `widl 11.10` defect found
en route: a `static(…)` attribute referencing a same-file interface **without** `contract(…)`
attributes segfaults widl (null contract deref); the interfaces carry contract attributes.

**Generator (`~/wa-work/tools/mk_activatable_reg.py`) — the emission is the COMPLETE union:**
winmd ids ∪ the 650 XAML creator-map ids ∪ the probed Controls answer set ∪ every
activation-table-index id ∪ the Wine builtin SYSTEM_MAP. The stale-deletion pass was a
silent no-op (its `]\t` pattern never matches system.reg's `] <hextime>` lines, so the
prefix accumulated generations and the winmd-only emission under-emitted); fixed to match
and to **wipe every 64-bit ActivatableClassId key, then re-emit the union** (Wow6432Node
untouched). From the 4122-key prefix: **4122/4122 recovered, 0 lost**, emitted **4265**
(`63 unmapped skipped`), incl. FontWeights, the FontWeight box, `Windows.ApplicationModel.AppInfo`,
`Windows.UI.Composition.Compositor`, `Windows.UI.Notifications.BadgeUpdateManager`,
`ToastNotificationManager`. Spot-checks pass (`Windows.Storage.ApplicationData` present,
FontWeights → `C:\windows\system32\windows.ui.dll`).

**The gap chain closed after the parse (each exposed by the previous fix, all evidence-driven):**
AppInfo and Compositor needed only keys (providers `windows.applicationmodel.dll` /
`windows.ui.composition.dll` already answered). `BadgeUpdateManager` and
`ToastNotificationManager` implemented in `twinapi.appcore.dll` (the Windows facade that
serves them on Windows): no-op badge/toast surfaces, `Setting=Enabled`, and a real empty
`IVectorView<ScheduledToastNotification>` (the app enumerates it; `E_NOTIMPL` throws
unhandled in the C# projection). `H.NotifyIcon.Core.TrayIcon.Create()` fail-fasted the host
(`e0464645`) on two `Shell_NotifyIconW` results Wine did not define: the `NIM_SETVERSION`
handshake (round-tripped to the tray window, unknown to the tray build) is answered locally,
and the entry-set operations are answered from the shell's own table — `add` succeeds,
`modify` requires the entry (the app's state toggle re-marshals with a **null owner window**,
accepted by the id-only match), `delete` succeeds once; a tray-less desktop answers the same
table. The app's own log confirms each step (`NotificationsManager`, `TrayIconManager`
entries cleared).

Measured after `a893911ff` (pristine app binaries, registry configuration only, probes
removed, no app binary patching):

| metric | tip `6d9288cef` | after |
| --- | --- | --- |
| `XAML parsing failed` (0x802b000a) | 1 per run | **0** |
| unresolved activations (`Failed to find library`) | 1 (FontWeights) | **0** (only `FakeClass.ClassName`, the app's own negative probe) |
| battery 8 × 60 s | 8/8 `rc=69` (post-parse managed death) | 8/8 `rc=69`, `fatal=0`, `av=0` (death now **after** tray init) |
| TrayIconManager / NotificationsManager fail-fasts | – | **cleared** (app-log verified) |
| `WinUIDesktopWin32WindowClass` | 0 | 0 (window not reached) |
| `WebView2Loader` / `msedgewebview2` | 0 / 0 | 0 / 0 (not reached) |
| screenshot `wa-work/wa_fontweights_after.png` | blank 295 B | blank 295 B (no XAML window; desktop root only) |
| tests | – | combase 0 fail, wintypes 0 fail, windows.ui 0 fail (top-level `check`) |
| app binaries | pristine | pristine (24da83be…, c6053f1a…, 9a42e19d…, 9353066e… == `.orig`) |

Next gaps: (a) the app's **managed `e0464645` FailFast after the tray path** — the
HybridDialog/WebView2 environment init is the wall (`WebView2Loader` never loads; the
activation layer is now silent); (b) the C# projection casts the returned `IVectorView` to
`IEnumerable` (closed pinterface QI) → `InvalidCastException` WARN (caught): answering that
IID would silence the last logged ERROR; (c) `WinUIDesktopWin32WindowClass` registration and
the webview2/edge surface: not reached.
