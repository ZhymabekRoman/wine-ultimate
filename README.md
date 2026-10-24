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


## The managed FailFast after the tray path: the WebView2 runtime wall, and the closed-pinterface cast

Tip `a893911ff` (battery 8/8 `rc=69`, `fatal=0`, `av=0`, app log stops right after a **caught**
`InvalidCastException` WARN that follows the `TrayIconManager`/`NotificationsManager` success
lines; CLR `e0464645`, `WebView2Loader`/`msedgewebview2` = 0, `WinUIDesktopWin32WindowClass` = 0).

**The FailFast exception identity (evidence, no app patching).** With `WINEDEBUG=-all,err+all,+combase`
the last two `RoOriginateLanguageException` calls on the faulting thread (`0024`) before
`RaiseFailFastException` are

```
RoOriginateLanguageException error 0x80070002, message L"",                            exception 0
RoOriginateLanguageException error 0x80070002, message L"Unable to find the specified file.", exception 00007F…
GetRestrictedErrorInfo (…)
record: code=e0464645 … (failfast_bt: coreclr frames → JIT frames)
```

i.e. one managed exception, created from HRESULT `0x80070002` (`Marshal.ThrowExceptionForHR` →
`System.IO.FileNotFoundException`, the bare resource message "Unable to find the specified file."),
then re-origined at the async boundary and left unhandled on the DispatcherQueue thread → `FailFast`.
`coreclr` never reaches `ReportEventW` (no `.NET Runtime` event, `+eventlog` shows only the event
service start) and the app's `applog.txt` records nothing after the WARN, so the identity is the
combase origination pair — not the (caught) `InvalidCastException`, which is a different thread
(`01b0`, `0x80004002`, logged and swallowed).

**Root cause: the WebView2 environment probe finds no browser.** The origination sits immediately
after `RoGetActivationFactory("Microsoft.Web.WebView2.Core.CoreWebView2Environment",
{0e33f804-f20b-5635-8491-162aaa27517b})` answers from the app's own `Microsoft.Web.WebView2.Core.dll`.
That assembly is the single-file WebView2 SDK: it imports `api-ms-win-core-registry` and locates the
runtime itself, so **no** `WebView2Loader.dll` load happens (`webview2loader=0` in every run — the
layer is entered, the loader DLL is simply not part of this SDK). The locator's probes, from `+reg`:

```
NtOpenKeyEx(… L"Software\Policies\Microsoft\Edge\WebView2\")     -> (nil)
NtOpenKeyEx(HKLM\Software -> Microsoft -> EdgeUpdate)            -> (nil)   (+Wow6432Node, +HKCU)
NtOpenKeyEx(… Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5})   -> never reached
```

Every one fails: the machine has **no Edge WebView2 runtime at all**
(`find /home/olge -iname 'msedgewebview2*'` → 0 hits, no `EdgeWebView` directory, no
`EdgeUpdate` registry key in the prefix), and the app's own `EBWebView/x64/` ships only
`EmbeddedBrowserWebView.dll` (the client DLL, not a browser). `CreateCoreWebView2EnvironmentWithOptions`
therefore returns `HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)` = `0x80070002`, the projection throws
`FileNotFoundException` in an `async void` initialiser, and the runtime fail-fasts. This one is
**not** reachable by Wine code: the browser binary has to exist.

**The caught cast: the closed pinterface QI is answered now.** The WARN stack is
`NotificationsManager.CancelScheduledNotifications` → `Enumerable.WhereEnumerableIterator.MoveNext`
→ `IReadOnlyListImpl`1.GetEnumerator` → `Make_IEnumerableObjRef` → `IObjectReference.As[T](iid)` →
`ThrowExceptionForHR`. The QI trace pins the id: `sched_vector_QueryInterface
{7a7b2a51-c182-5846-a861-4f9c036f24ad}` =
`IID___FIIterable_1_Windows__CUI__CNotifications__CScheduledToastNotification`
(the closed instantiation of the `IIterable`1` pinterface over the element class, computed by
`widl` into `include/windows.ui.notifications.h`). The projection maps `IEnumerable<T>` to
`IIterable<T>`; `E_NOINTERFACE` on the id is turned into `InvalidCastException` in the app.

**Fix (source, `ea75cf587`, `twinapi.appcore`).** The scheduled-toast vector view now answers the
closed `IIterable<ScheduledToastNotification>` on the **same object** (canonical `IUnknown`/
`IInspectable` identity stays the vector view iface, as ComWrappers requires) and its `First()`
hands out a real, already-exhausted `IIterator<ScheduledToastNotification>`: `get_HasCurrent` FALSE,
`MoveNext` FALSE, `GetMany` → 0 items, `get_Current` → `E_BOUNDS`, matching the empty scheduled set.
New `test_ToastNotificationManager` in `dlls/twinapi.appcore/tests/twinapi.c` covers the activation,
the two closed-pinterface QIs, the identity, and the empty-enumeration contract.

Measured after `ea75cf587` (app binaries re-verified byte-identical to `.orig`: CoreMessagingXP,
FrameworkUdk, Microsoft.ui.xaml, dwmcorei all `PRISTINE`; probes removed, registry configuration only):

| metric | tip `a893911ff` | after |
| --- | --- | --- |
| app log `InvalidCastException` / `ERROR` lines | 1 per run | **0 per run** (10 runs) |
| battery 8 × 60 s | 8/8 `rc=69` | **3/8 `rc=124` alive**, 5/8 `rc=69`; `fatal=0`, `av=0` |
| battery 3 × 150 s | 0/3 alive | **1/3 `rc=124`**, 2/3 `rc=69` (the failfast is now raced by the tray/webview ordering) |
| `WebView2Loader` / `msedgewebview2` | 0 / 0 | 0 / 0 (the SDK never loads a loader DLL; the runtime is absent) |
| `WinUIDesktopWin32WindowClass` | 0 | 0 (not reached) |
| screenshot `wa-work/wa_managed_after.png` (t=125 s) | blank 295 B | blank 295 B (desktop root only) |
| tests (`twinapi.appcore`, `combase`, `windows.ui`) | – | **0 failures** (direct `runtest`, harness prefix, x86_64 and i386); `wintypes` 1 pre-existing `IBufferByteAccess_Buffer returned NULL data` |

Next gaps: (a) **the WebView2 runtime is the wall** — install the Edge WebView2 runtime into the
prefix (`Program Files (x86)\Microsoft\EdgeWebView\Application\<ver>\msedgewebview2.exe` plus
`…\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` `pv` keys, HKLM/WOW6432Node/HKCU),
then the frontier is the `msedgewebview2.exe` spawn + browser IPC handshake (nothing to spawn exists
on this machine). (b) `IReference`1<T>` **boxing** closed pinterfaces are still refused on Wine's
`PropertyValue` statics — 8 ids, 606 requests per run, all caught: `e7beaee7-160e-50f7-8789-d63463f979fa`
(145×), `64bd43f8-bfee-4ec4-b7eb-2935158dae21` (104×), `02dd3ad0-b9de-4b55-a0c3-507235eae8ea` (104×),
`0c881986-86e1-40b3-9642-9e5090a8a6b0` (82×), `dc678962-0653-4c92-92cd-95943d593900` (61×),
`1f8cc8ba-0cc5-4799-a505-f358c9886d12` (41×), `6e704a95-4b2f-5ba8-ada5-1261c832baed` (22×),
`3830ad99-d8da-53f3-989b-fc92ad222778` (22×, identified as the `ABI.System.Nullable_Type` vtable id in
`WinRT.Runtime.dll`, i.e. the `System.Type` box); all eight blobs are declared in `Microsoft.ui.xaml.dll`
(the WinUI value-type boxes) — answering them on `wintypes.dll`'s `PropertyValue` removes the last
`fixme` flood. (c) `{11d3b13a-180e-4789-a8be-7712882893e6}` / `{5c13e51c-4f32-4726-a3fd-f3edd63da3a0}`
are .NET-side probes asked of every WinRT object (both present in the pristine `twinapi.appcore.dll`,
so Windows answers them); `E_NOINTERFACE` is benign, no app-log noise. (d) `WinUIDesktopWin32WindowClass`
registration: still not reached.

## WebView2 Runtime deployment (env-prep, signed redistributable, zero binary patching)

Runtime deployed into the prefix from Microsoft's signed redistributables (no app binaries touched;
`CoreMessagingXP 24da83be`, `FrameworkUdk c6053f1a`, `Microsoft.ui.xaml 9a42e19d`, `dwmcorei 9353066e`
re-verified byte-identical to `.orig` at the end of this session; no probe files in the app dir).

Installer identity (both fetched over TLS from Microsoft; `linkid=2108954` in the task brief is stale —
it 302s to bing):

- Evergreen **offline** installer x64: `https://go.microsoft.com/fwlink/?linkid=2124701` →
  `msedge.sf.dl.delivery.mp.microsoft.com/.../MicrosoftEdgeWebView2RuntimeInstallerX64.exe`,
  214,823,632 bytes, sha256 `9b0142e22e8daa92855d746ae4d1e9eeef561ccaf67d1dd452165c4af3df3191`.
  Authenticode: leaf `CN=Microsoft Corporation` (serial 33000002334EC248564C2B5C6F000000000233,
  notBefore 2026-04-16), issuer `Microsoft Code Signing PCA 2024` (sha256 fp `3DADFAF8…142F8F`),
  chain roots at `Microsoft Root Certificate Authority 2011`; leaf-over-intermediate signature
  verified with `openssl verify -partial_chain` (rc 0); embedded SignedData digest
  `C245F07F27DDDE24CD4B367282E9B59A37006DFFD3E5FB552E772823263752AD` (sha256). The 2011 root blob is
  unreachable from this network (`www.microsoft.com/pki` blob deleted, `ctldl.windowsupdate.com`
  blocked, `crl.microsoft.com` 404), so the chain is recorded, not rooted.
- The offline installer self-extracts EdgeUpdate binaries to
  `Program Files (x86)\Microsoft\Temp\EU759e.tmp` and stalls in its elevation/progress flow under
  wine (540 s timeout, no msiexec, no `+exec` child) — not usable headless.
- Deployed runtime instead: **MSIX** `https://go.microsoft.com/fwlink/?linkid=2124700` →
  `MicrosoftWebView2Runtime_157.0.4341.16_Beta.msix`, 299,693,184 bytes (285.8 MiB), sha256
  `bcd13a9e20ee315af02ce78e23b4e98a7fa216e0c785d5c2d0fc16abdc20e653` (Beta-channel 157.0.4341.16,
  signed package). Unzipped (MSIX = zip) and file-deployed to
  `C:\Program Files (x86)\Microsoft\EdgeWebView\Application\157.0.4341.16\` (72 entries, 662 MB,
  `msedgewebview2.exe` launcher stub + `EBWebView\x64\EmbeddedBrowserWebView.dll` present) with the
  version manifest at `…\Application\157.0.4341.16.manifest`.
- Registry (wine `regedit /s` — plain `wine regedit FILE` silently no-ops, `/s` is required):
  `pv=157.0.4341.16`, `path=…\Application`, `exe/name/dr/usagestats`, and `EBWebView` (REG_EXPAND_SZ,
  hex(2); `str(2):` syntax is rejected) under
  `HKLM\Software\Microsoft\EdgeUpdate\{Clients,ClientState}\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`,
  `HKLM\Software\WOW6432Node\…` same, `HKCU\Software\…` same.

Outcome (150 s runs, tip `ea75cf587`):

- The original `System.IO.FileNotFoundException 0x80070002` FailFast **at ~20 s is gone**; the app now
  survives to **t≈140 s** and reaches the **XAML window-proc path**: the managed fail-fast backtrace
  maps `Microsoft.ui.xaml.dll+0x1766e7 → user32 window proc → JIT managed frames → coreclr FailFast`
  with 19 native frames inside **wine `d2d1.dll`** (fail-fast record addr `d2d1+0x2ea399`, nested
  `e0434352`, FailFast reason word `0xc0000482` = type-load-unachievable) fired right after the
  `HybridTrayIconContextMenu` string loads + tray context-menu window creation. A `0x8004002
  "Specified cast is not valid."` origination (XAML thread) precedes it.
- The WebView2 probe (combase origination pair `0x80070002` on the main thread, now at t≈148 s)
  **persists**: `+reg` shows the app walking `EdgeUpdate\ClientState\{F3017226-…}` and reading the
  `EBWebView` value successfully (4 views per iteration), but **no file I/O to the runtime folder
  ever follows** (zero `msedgewebview2`/`EdgeWebView` opens on `+file`, zero `EmbeddedBrowserWebView`
  `LoadLibrary`, zero `+exec`/process spawn; server-side file/exec traces emit nothing during runs —
  `wineserver -f` logs only its exit object dump). Tried `EBWebView` = `…\Application`,
  `…\Application\157.0.4341.16`, and `…\Application\157.0.4341.16\EBWebView` — the walk is
  byte-identical each time. The .NET SDK's own file probes (`NtQueryFullAttributesFile`-style) are
  invisible to the `+file` channel; the SDK consumes the `EBWebView` string and rejects it without
  filesystem follow-up. The exact `EBWebView` value semantics the loader demands remain the open
  question (next probe: instrument `WebView2Loader.dll` `CreateCoreWebView2EnvironmentWithOptions`
  with btsymbol/breakpoints, and dump the managed `FileNotFoundException` via mscordac on a parked
  crash — the app ships `mscordaccore.dll`/`mscordbi.dll`).
- Numbers: `wa_wv2.sh` 150 s: `rc=69 DIED`, `e0464645×3`, `msedgewebview2` procs in `ps` = **0**,
  `exec:` spawn lines = 0, named-pipe lines = 0, windows 216–238 (tray/IME class), XAML desktop
  window **not mapped**; screenshot `wa-work/wa_webview_after.png` (t=125 s) **blank 295 B** (desktop
  root only). Battery 8×60 s: **1/8 `rc=124` alive**, 7/8 `rc=69` (deterministic fail-fast at log
  line 721 every run; baseline was 3/8), `webview2loader=0`, `edge=0`, `fatal=0`, `av=0`.

Next gaps: (1) the **d2d1/type-load fail-fast in the XAML window-proc render path** is now the first
crash (t≈140 s; battery-limiter at ~55 s) — symbolize the JIT frames (btsymbol + mscordac on a
minidump; `DOTNET_DbgEnableMiniDump` env is ignored by this app); (2) the WebView2 `EBWebView` probe
semantics above; (3) `msedgewebview2.exe` spawn + browser IPC (nothing spawned yet; the launcher stub
+ `EBWebView` per-client deploy is on disk, ready).

### WebView2 SxS (Beta) client GUID registration — locator acceptance (2026-10-24)

The app's single-file `Microsoft.Web.WebView2.Core.dll` locator probes BOTH EdgeUpdate
client GUIDs (UTF-16 strings in the assembly):
`{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}` (Evergreen stable) and
`{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}` (SxS/Beta). The deployed MSIX
`MicrosoftWebView2Runtime_157.0.4341.16_Beta.msix` is a **Beta** package — its keys
must be registered under the **SxS GUID**, not only the stable one. Registration
(`wine regedit /s`, then `wineserver -k` to flush; `pv` as REG_SZ, `dr` as hex(2)):

- `HKLM\Software\Microsoft\EdgeUpdate\Clients\{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}`
  pv=157.0.4341.16, path=C:\Program Files (x86)\Microsoft\EdgeWebView\Application\157.0.4341.16,
  exe=<path>\msedgewebview2.exe, name="Microsoft Edge WebView2 - Beta", dr=%LOCALAPPDATA%\Microsoft\EdgeWebView\EBWebView, usagestats=1
- `ClientState\{56EB18F8…}`: pv, path, EBWebView="x64" (locator checks <path>\EBWebView\x64\EmbeddedBrowserWebView.dll — present)
- Same under `HKLM\Software\WOW6432Node\…` and `HKCU\…`.

Measured effect: `msedgewebview2.exe` spawns (2 processes, alive through 135 s) for the
first time; the `FileNotFoundException` locator FailFast is gone; app survives 135 s.
Remaining walls: XAML window (WinUIDesktopWin32WindowClass) not mapped, no Navigate in
applog, browser IPC handshake (msedgewebview2 child tree: gpu/renderer/utility + named
pipes) not completed.

## CoreMessaging registrar anatomy + session-id / ProcessMessage fixes (2026-10-24)

Source (github `ultimate`, linear on `ea75cf587`):

- `3f84f522d` server, kernel32: named-pipe session-id connection attributes —
  `pipe_end` carries `client_session_id`/`server_session_id` (from the creating
  process, copied at connect, visible on both ends), `FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE`
  answers `"ClientSessionId"`/`"ServerSessionId"`, and kernel32
  `GetNamedPipeClientSessionId`/`GetNamedPipeServerSessionId` become the
  `NtFsControlFile` round trip (no more self-referential PEB stubs). Chosen because
  Chromium-family IPC verifies the pipe peer's session id during the WebView2 mojo
  handshake — now on the critical path once the browser spawns.
- `d87281559` coremessaging: loopback `ProcessMessage` completes the message in place
  (header + TLVs echoed, `S_OK`) — a negative completion is **fatal** for the callers:
  the XP dispatch layer answers it with `CFlat::Abandonment::FailWithHR` →
  `RaiseFailFastException(0xe0464645)`. Conversation test updated to the positive
  contract. Same commit: `NtAlpcSendWaitReceivePort` frame traces (port, lpc type,
  msg id, verb, subid, 48-byte hex prefix) and a server relay print gated by
  `WINE_ALPC_RELAY_TRACE` (sender tid, stamp, msg id, handle dup).

The t≈140 s fail-fast is now fully symbolized (`CoreMessagingXP.dll.i64` + objdump):
`Dispatcher::Callback_DispatchLoop → DeferredCall::Callback_Dispatch →
Registrar::AlpcClientAdapter::Callback_ProcessIncoming →
AlpcConnection::Callback_HandleReceivedBuffer` — the type-11 branch calls the
connection host's vtable+24 with the connection context, and a negative HRESULT
triggers `FailWithHR(nullptr, 0)` → `RaiseFailFast` with reason word `0xc0000482`
(STATUS_FATAL_APP_EXIT). Registrar wire anatomy measured via the new traces:
frame = `[u32 total][u16 verb][u16 subid][TLV…]`, **verb 1 = request, 2 = reply,
16 = reverse notification**; connect notifications ride as type-10 items. The main
thread's registrar client connection receives **two verb-16 ReleaseConversationClient
notifications (target u32 = 0x138 = the engine's own registrar server thread) queued
by the engine server at connect time, right behind its `RegisterConversation` reply**
(id 5). They are delivered at t≈140 s, the host parses them negatively, and the process
fail-fasts. That is the wall gating both the XAML window and the WebView2 environment
probe (t≈148 s). The builtin registrar provider registers fine (client id 2) and
receives **zero** routed requests — the engine self-serves `CoreUICore` joins in-process.
WebView2 state in this build: the app loads `Microsoft.Web.WebView2.Core.dll`
(+Projection, +LocalizationResources), never `WebView2Loader.dll`, never
`EmbeddedBrowserWebView.dll`, zero `msedgewebview2` exec — all behind the XAML wall.

Measured after both commits (app binaries re-verified `PRISTINE`: CoreMessagingXP
`24da83be`, Microsoft.ui.xaml `9a42e19d`, dwmcorei `9353066e` == `.orig`;
WebView2Loader `4cd3ffd5…`, Microsoft.Web.WebView2.Core `799b8c69…` == start hashes;
no probe files in the app dir):

| metric | before | after |
| --- | --- | --- |
| `test_namedpipe_session_id` (kernel32 pipe, full 38 466 tests) | stub | **passes**; 16 failures = pre-existing `ImpersonateNamedPipeClient` pair (pipe.c:1984/2034) |
| `coremessaging_test` failures | 19 (flaky IAsync set) | 31 on reruns — **zero new rows** (diff vs baseline: only the updated conversation assertions pass) |
| `wa_wv2.sh` 150 s | `rc=69`, `e0464645×3`, edge 0, routed 0, windows 238, no XAML window | identical (`rc=69`, edge 0, routed 0, windows 238) |
| battery 8 × 60 s | 8/8 `rc=69` baseline | **8/8 `rc=69`, `fatal=0`, `av=0`** (no regression) |
| screenshot `wa-work/wa_webview_after.png` (t=110 s) | blank 295 B | blank 295 B (desktop root only) |

Next gaps: (a) capture the failing host HRESULT and object identity at
`CoreMessagingXP+0x785c3` — in-wine `winedbg` attach to the live app returns
**error 87** while registrar ALPC receives are in flight (debugger-attach vs async
receive interaction: the next probe); (b) the spurious verb-16 (`ReleaseConversationClient
0x138`) emitted by the engine server at `RegisterConversation` is the candidate trigger —
cheap experiment: make the builtin provider connect **once** (no 25 ms retry loop; the
three failed `NtAlpcConnectPort` attempts may create/destroy registrar client entries
that the server then releases); (c) once the wall clears: `WebView2Loader` load →
`msedgewebview2` spawn → mojo named pipes (session-id attributes now land).

## The XAML desktop window is created; the wall is the WebView2 browser locator (2026-10-26)

Window anatomy, from `+class` / `+combase` / `+file` / `+reg` traces on pristine binaries
(tip `04609dd94`, `WINEDEBUG=-all,err+all,+class`):

- WinUI3's window-class registration is intact and **is reached**. Microsoft.ui.xaml.dll
  RVA `0x213c80` is the registration function: `GetClassInfoExW(hinst, L"WinUIDesktopWin32WindowClass")`
  → zeroed `WNDCLASSEXW` (cbSize 0x50) → `LoadIconW(NULL, 0x7f00)` → `RegisterClassExW`
  with `lpfnWndProc = Microsoft.ui.xaml+0x219440`; the caller at `0x213bf0` is the
  `FindWindowExW(NULL, L"WinUIDesktopWin32WindowClass", …)` singleton probe. Those are the
  only two xrefs of the class string (`0x180a72218`).
- Measured in-app: `trace:class:GetClassInfoExW L"WinUIDesktopWin32WindowClass" … -> not found`,
  `trace:class:NtUserRegisterClassExWOW name=L"WinUIDesktopWin32WindowClass" hinst=0x6ffffa800000
  style=0x3 … atom=c03a wndproc=0x6ffffaa19440`, then
  `NtUserCreateWindowEx created window 0x1006c class L"WinUIDesktopWin32WindowClass" name L"WinUI Desktop"`,
  and the whole WinUI windowing/content/input stack: `ReunionWindowingCaptionControls`
  (`ReunionCaptionControlsWindow`), `Microsoft.UI.Content.DesktopChildSiteBridge`,
  `InputSiteWindowClass`, `InputNonClientPointerSource` (`Non Client Input Sink Window`),
  `DcppUserAdapterWindowClass`, dcomp `DXGI device window`s, `MITMessageOnlyWindowClass`.
  The earlier note "WinUIDesktopWin32WindowClass never registered" is superseded: it registers,
  the window is created, and it is never shown (no `SWP_SHOWWINDOW` for that hwnd — the app
  does not reach `Window.Activate()`).
- The wall behind the window is a .NET `FileNotFoundException` that the app turns into a
  fail-fast every ~4 s: `trace:combase:RoOriginateLanguageException error 0x80070002,
  message L"Unable to find the specified file."` right after the app activates
  `Microsoft.UI.Xaml.Controls.WebView2` plus `Microsoft.Web.WebView2.Core.CoreWebView2Environment{,Options}`
  and starts building its `HybridDialog…UnexpectedError…` UI
  (`resource_loader_GetString L"HybridDialogRestartButton"` …). 33 fail-fasts
  (`RaiseFailFastException code=e0464645`) per 150 s, 3 startup cycles, app exits `rc=69`,
  `sadRecord.dat` keeps `[2.2620.102] VisibleWindow > VoipInitCtor+…`, screenshot blank.
- Cause, from the `+reg` probe trace: the WebView2 browser locator opens
  `…\Microsoft\EdgeUpdate\ClientState\{BE59E8FD-089A-411B-A3B0-051D9E417818}`
  (the **stable Edge** client GUID) under HKLM, WOW6432Node and HKCU. Only the WebView2
  runtime GUIDs `{F3017226-…}`/`{56EB18F8-…}` were registered, so the open fails and the
  locator reports `ERROR_FILE_NOT_FOUND`.

### Env-prep: stable Edge client GUID (registry only, no binary patching)

`wa-work/envprep_edge_stable.reg` (`wine regedit <file>`, then `wineserver -k`) registers
`{BE59E8FD-089A-411B-A3B0-051D9E417818}` as `Clients` **and** `ClientState` under
`HKLM\SOFTWARE\Microsoft\EdgeUpdate`, `HKLM\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate` and
`HKCU\SOFTWARE\Microsoft\EdgeUpdate`, pointing at the already-deployed signed 157.0.4341.16
tree (`msedge.exe`, `msedgewebview2.exe`, `EBWebView\x64\EmbeddedBrowserWebView.dll` all present):
`pv`, `path`, `exe`/`exes`/`exepath`, `name`, `version`, `usagestats`, and — for the
Edge client the value is a **data path**, not the `x64` architecture token that the WebView2
runtime GUIDs use — `EBWebView="C:\Users\olge\AppData\Local\Microsoft\Edge\EBWebView"` plus
`dr="…\Microsoft\Edge\BrowserMetrics"` (both directories created under the prefix). With
`EBWebView="x64"` the locator opens the key successfully and then fails the value check,
which re-produces the same `0x80070002` one layer deeper.

Measured effect of the GUID registration alone (150 s, pristine binaries):
`rc=124 ALIVE_150s`, `Fatal error=0`, `RaiseFailFastException=0`, `e0464645=0`
(was `rc=69`, 33 fail-fasts), `RoOriginateLanguageException … 80070002 = 0` (was 2), and the
`EdgeUpdate` probes stop. The app no longer crash-loops; it now idles **before** creating the
XAML window, which exposes the next wall.

### Env-prep: the app's own ActivatableClassIds (registry only)

The app's manifest declares 113 activation classes with **no module attribute**
(`<ActivatableClass ActivatableClassId="WhatsAppNative.Voip.VoipFactory" ThreadingModel="both"/>`),
and the framework generator regenerates
`HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId` from the framework union only, so
every app-declared class is unregistered: combase answers
`0x80040111` for `WhatsAppNative.Voip.VoipFactory`, `WhatsAppNative.Sqlite`,
`WhatsAppNative.MiscCpp`. `wa-work/tools/mk_app_activatable_reg.py` derives the implementing
module from the binaries (module exports `DllGetActivationFactory` **and** carries the class id
as UTF-8/UTF-16; the hit set is unique for all 112 emitted ids) and emits
`DllPath`/`TrustLevel`: `WhatsAppNative.Voip.dll` 52 ids, `WinRTAdapter.dll` 40,
`WhatsAppNative.dll` 20. Only
`Microsoft.Windows.ApplicationModel.Background.UniversalBGTask.Task` has no module on disk
(not shipped by WindowsAppRuntime 1.6 in this prefix).

With both registrations the app gets back to the XAML window (`WinUIDesktopWin32WindowClass`
+ caption/bridge/input windows created) and the WebView2 locator wall is the first failure
again — i.e. the app-class registration is a strict advance, and the remaining gate is the
browser locator / environment handshake.

### Measured chain of gate runs (150 s each, pristine app binaries, tip `cf7736bca`)

`wa-work/wa_gate.sh 150` = `WINEDEBUG=-all,err+all,+combase,+file,+reg,+loaddll,+exec,+named_pipe`
plus a t=70 window/ps census and a screenshot:

| # | env-prep state | rc | `RaiseFailFast` | `e0464645` | `80070002` originations | XAML window | `WebView2Loader` load | `msedgewebview2` | X11 app window |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | tip only | `69` DIED | 33 | 3 | 2 | created (`WinUI Desktop`) | 0 | 0 | none |
| 2 | + Edge stable GUID (`path`=version dir, `EBWebView=x64`) | `124` ALIVE_150s | **0** | **0** | **0** | not created (app idles earlier) | 0 | 0 | none |
| 3 | + app `ActivatableClassId`s | `69` DIED | 33 | 3 | 2 | created | 0 | 0 | none |
| 4 | + `EBWebView` = absolute data path (REG_SZ) | `69` DIED | 33 | 3 | 2 | created | 0 | 0 | none |
| 5 | + `EBWebView`/`dr` as REG_EXPAND_SZ | `69` DIED | 33 | 3 | 2 | created | 0 | 0 | none |
| 6 | + `path` = Application root, `EBWebView=x64`, `Application\EBWebView -> 157.0.4341.16\EBWebView` symlink | `69` DIED | 33 | 3 | 2 | created | 0 | 0 | none |

The registry-only fixes move real distance (run 2: the fail-fast loop and the `0x80070002`
disappear; run 3: the app reaches XAML window creation and the whole WinUI windowing stack
appears), and they pin the remaining gate precisely:

- In the last 400 traced calls before the exception the **only** registry value the app reads is
  `EBWebView` — six reads over the three roots — and every one succeeds.
- Across the whole 120 112-line run there is **not one** file probe mentioning
  `EdgeWebView`, `EmbeddedBrowserWebView.dll` or `msedgewebview2.exe`, no `exec:` line at all,
  and no `WebView2Loader.dll` load: the composition `<path>\EBWebView\x64\EmbeddedBrowserWebView.dll`
  is never turned into a filesystem query. The `0x80070002` is produced by the managed locator
  deciding "no usable runtime" from the registry data alone.
- So the four registry-layout permutations (key present, value type, `path` root vs version dir,
  `EBWebView` as arch token vs data path, symlink satisfying both compositions) all reproduce the
  identical wall; the decision is inside the managed locator, not in the registry layout.

Next gaps (ordered, with the exact place to cut):

1. **The managed locator's decision** (`Microsoft.Web.WebView2.Core.dll`, 1.0.3485.44): the
   strings `EBWebView\x64\EmbeddedBrowserWebView.dll`,
   `Software\Microsoft\EdgeUpdate\ClientState\`,
   `Software\Microsoft\EdgeUpdate\Clients\{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}` and
   `WebView2: CoreWebView2Environment failed when trying to call into EmbeddedBrowserWebView.dll. hr=0x`
   are in the assembly; the probe order measured is `Clients\{56EB18F8…}` → `EBWebView` on the
   three roots → `ClientState\{BE59E8FD…}` → throw. Reading that IL (ilspycmd/dotnet-ildasm on the
   single-file Core assembly) gives the exact value set the locator demands; a Wine-side
   `+reg`-visible answer for `pv`/`path`/`EBWebView` on the right root is the fix. The app dir
   already carries `Microsoft.Web.WebView2.Core.dll.i64` for the native side.
2. **The window is created, never activated**: no `SWP_SHOWWINDOW`/`ShowWindow` for the
   `WinUIDesktopWin32WindowClass` hwnd (`0x1006c`), so nothing maps on X11 — the only mapped
   top-level for the process is Wine's per-process desktop window (`960x768+0+0`,
   WM_CLASS `whatsapp.root.exe`). Screenshot verdict: **blank desktop** (295 B uniform at t=70/110).
   `Window.Activate()` is gated behind the WebView2 environment, so 1 unblocks it.
3. After the environment succeeds: `WebView2Loader.dll` load → `msedgewebview2.exe` spawn
   (proven once, 2 processes for 135 s, with the SxS GUID layout) → `--mojo-named-platform-channel-pipe`
   first-pipe counts + async `ConnectNamedPipe` + the session-id attributes (`3f84f522d`), then the
   child tree (gpu/renderer/utility) and the handshake → `CreateCoreWebView2Controller` → `Navigate`.
4. `Microsoft.Windows.ApplicationModel.Background.UniversalBGTask.Task` has no implementing module
   on disk (not shipped by the WindowsAppRuntime 1.6 package in this prefix).
5. Pre-existing kernel32 `ImpersonateNamedPipeClient` test pair (16 failures) — not on the
   Chromium named-platform-channel path, worth closing for the pipe suite.

Applied env-prep state, reproducible (registry + prefix files only; app binaries untouched,
hashes re-verified after every run):

```bash
# 1. stable Edge client GUID (Clients + ClientState x HKLM / WOW6432Node / HKCU)
wine regedit /home/olge/wa-work/envprep_edge_stable.reg
# 2. the app package's own 112 activation classes
python3 -I /home/olge/wa-work/tools/mk_app_activatable_reg.py
wine regedit /home/olge/wa-work/envprep_app_activatable.reg
# 3. the locator's composed layout
mkdir -p "$WINEPREFIX/drive_c/users/olge/AppData/Local/Microsoft/Edge/EBWebView" \
         "$WINEPREFIX/drive_c/users/olge/AppData/Local/Microsoft/Edge/BrowserMetrics"
ln -sfn 157.0.4341.16/EBWebView \
        "$WINEPREFIX/dosdevices/c:/Program Files (x86)/Microsoft/EdgeWebView/Application/EBWebView"
wineserver -k   # flush the registry hives
```

### Verification with the full env-prep applied (app classes + Edge GUID + layout)

| metric | value |
| --- | --- |
| `wa_verify150.sh` 150 s | `rc=69` (app exits itself), `Fatal error=0`, `c0000005=0`, `WebView2Loader=0`, `msedgewebview2=0`, X windows 256 (all `Default IME`/shell) |
| `wa_shot_run.sh` 150 s | `rc=69`, `Fatal error=0`, app fail-fast lines 48, windows created 42, screenshot **295 B blank** at t=125 s, census: 253 `Default IME`, 2 `DXGI device window`, 1 `dxgi_test` — no app top-level |
| `wa_battery.sh 8 60` | 7/8 `rc=69`, 1/8 `rc=124 ALIVE_60s`, `fatal=0` in all 8, `webview2loader=0`, `edge=0` (tip baseline was 8/8 `rc=69`, `fatal=0`) |
| app binaries after all runs | `CoreMessagingXP 24da83be == .orig`, `dwmcorei 9353066e == .orig`, `Microsoft.ui.xaml 9a42e19d`, `WebView2Loader 4cd3ffd5`, `Microsoft.Web.WebView2.Core 799b8c69` — PRISTINE |

Screenshot verdict: **blank** (uniform desktop, no browser, no QR). The XAML window is created
inside Wine but never activated, so nothing maps; the only mapped top-level with
WM_CLASS `whatsapp.root.exe` is Wine's per-process desktop window (`960x768+0+0`).

Trade-off recorded: with the Edge GUID alone the app is quiet (`ALIVE_150s`, 0 fail-fasts) but
idles before the XAML window; registering the app's own activation classes is the correct state
(on a real install those classes exist) and moves the app to the real gate — XAML window
created, WebView2 environment probe reached, `0x80070002` from the managed locator, fail-fast
loop, `rc=69`. The battery metric is unchanged (7/8 `rc=69` + 1 alive vs 8/8 `rc=69`), so the
advance costs no stability; it just exposes the gate that was previously hidden.

## The locator decompiled: registry acceptance recipe, the DESKTOP_APP_POLICY bug, and the ARM64 payload wall

`Microsoft.Web.WebView2.Core.dll` (793 176 B, app dir) is **native x64** — ilspycmd: "PE file
does not contain any managed metadata". It is the WinRT class factory for
`Microsoft.Web.WebView2.Core.CoreWebView2Environment`; the managed assembly beside it,
`Microsoft.Web.WebView2.Core.Projection.dll` (685 168 B, i386 Mono), is pure CsWinRT glue
(ilspycmd, 997 files, zero decision logic). Chain: `RoGetActivationFactory` →
`sub_180001FC8` (CreateCoreWebView2EnvironmentWithOptions) → `sub_180002249` →
`sub_180001844` (GetAvailableWebView2Paths).

**Exact acceptance test** (all from the disassembly, no IL involved). With no
browserExecutableFolder, channels are enumerated in order **4,3,2,1,0** (`sub_180001E5C`,
mask 31 with default options): 4 `{BE59E8FD-089A-411B-A3B0-051D9E417818}` (Edge stable),
3 `{65C35B14-6C1D-4122-AC46-7148CC9D6497}` (canary), 2 `{0D50BFEC-CD6A-4F9A-964C-C7416E3ACB10}`
(dev), 1 `{2CD8A007-E189-409D-A2C8-9AF4EF3C72AA}` (beta), 0 `{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`
(WebView2 runtime stable). Per channel, HKLM **then** HKCU
`Software\Microsoft\EdgeUpdate\ClientState\<GUID>` (RegOpenKeyExW access 0x20219 =
KEY_READ|KEY_WOW64_32KEY; **value type not checked**, REG_SZ and REG_EXPAND_SZ both accepted),
reading exactly one value, **`EBWebView`**:

1. non-empty (cbData ≥ 3);
2. its **last `\`-separated component parses as N.N.N.N** (`sub_1800027F6`, 4× wcstol, '.'
   separators) — the value must BE the versioned directory, not the Application root, and not
   the arch token `x64` the previous recipe used (`pv`/`path`/`exepath` are never read in this
   mode);
3. numeric component-wise version ≥ **86.0.616.46** (the aV_0 dwords {86,0,616,46} at
   0x180091b18, `sub_180002AF5`; "WebView2: skipped an incompatible version" is printed
   through OutputDebugStringA otherwise);
4. `GetFileAttributesW` + `CreateFileW(0x100021, share 7, OPEN_EXISTING)` on
   `<value>\EBWebView\x64\EmbeddedBrowserWebView.dll` (`sub_180001076`; "WebView2: skipped
   inaccessible" on failure).

Accept ⇒ browserExecutableFolder = the value; then `LoadLibraryW('<value>\EBWebView\x64\
EmbeddedBrowserWebView.dll')` + `GetProcAddress("CreateWebViewEnvironmentWithOptionsInternal")`
(`sub_180003E18`). All channels failing (after a packaged-runtime fallback via
TryCreatePackageDependency/AddPackageDependency on families `Microsoft.WebView2Runtime.{Stable,
Beta,Dev,Canary,Internal}_8wekyb3d8bbwe`, absent in Wine) ⇒ OutputDebugStringA "WebView2:
Failed to find an installed WebView2 runtime or non-stable Microsoft Edge installation." ⇒
**0x80070002** — the RoOriginateLanguageException wall. `Clients\{56EB18F8-…}` mode
(`location` + '\' + `pv`) exists only for index 0 and only when the options object sets the
legacy Edge-beta flag.

Recipe applied (`/home/olge/wa-work/envprep_wv2_locator.reg`): `EBWebView` =
`C:\Program Files (x86)\Microsoft\EdgeWebView\Application\157.0.4341.16` on
ClientState\{F3017226…} HKLM+HKCU; `location` = Application root on Clients\{56EB18F8…}
HKLM+HKCU. Filesystem was already compliant (MSIX deploy: version dir + `EBWebView\x64\
EmbeddedBrowserWebView.dll`). **Accepted** — gate evidence: `loaddll` `Loaded "C:\Program Files
(x86)\Microsoft\EdgeWebView\Application\157.0.4341.16\EBWebView\x64\EmbeddedBrowserWebView.dll"
… native`, `CreateWebViewEnvironmentWithOptionsInternal` executes, the host creates the mojo
bootstrap server pipe `\\??\\pipe\\LOCAL\\mojo.<pid>.<nonce>` (CreateNamedPipeW openMode
0x40080003), and `RoOriginateLanguageException 0x80070002` count drops to **0**.

**Wine bug on the next step (fixed, wine-ultimate `7e83b8a56`)**: the loader launches the
browser with STARTUPINFOEX and `UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY
= 0x00020012, DWORD)`; Wine's `validate_proc_thread_attribute()` hit `FIXME … Unhandled
attribute 18` → ERROR_NOT_SUPPORTED → the launch aborted **before CreateProcessW**, environment
creation returned **0x80070032**, the managed side got a NullReference → `e0464645` FailFast
(every ~4 s). The kernelbase fix validates the size and ignores the policy (no appcontrol in
Wine), mirroring EXTENDED_FLAGS. After the fix the launch reaches
`CreateProcessInternalW L"…\msedgewebview2.exe" --embedded-browser-webview=…` →
`NtCreateUserProcess` → **0xc000007b**.

**New wall, env-prep not Wine: the deployed runtime payload is ARM64.** `msedgewebview2.exe`
machine = 0xAA64 (wv2.msix is the ARM64 package; only `EBWebView\x64\EmbeddedBrowserWebView.dll`
is x64, which is why the locator's dll loads fine). `MicrosoftEdgeWebView2RuntimeInstallerX64.exe`
is an **online bootstrapper**: its `.rsrc/0/B/102` stream is a GoogleUpdate CMS/tar manifest
(magic d183000/d0c5e3c, ustar entries) whose `msedge_wv2_x64.cab` entry is 015266331703₈ bytes
remote; the file carries only a 5.9 MB updater cab at EOF — no offline x64 browser.

| metric | value |
| --- | --- |
| `wa_gate.sh 150` post-fix | `rc=124 ALIVE_150s`, `fatal=0`, `RaiseFailFastException=0`, `e0464645=0`, originate=3 (legacy 0x80040111 MiscCpp/Sqlite/Voip only), `orig_80070002=0`, `orig_80070032=0` |
| `wa_gate.sh 210` | `rc=124 ALIVE_210s`, ff=0; XAML window not created in these runs (DcppUserAdapter + 960x768 desktop window + D3D11 draws; app idles after startup) |
| deep-trace 120 s run | env chain at t≈140 s: EB dll load → mojo pipe → `UpdateProcThreadAttribute 00020012` accepted → `PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY 2` → `NtCreateUserProcess msedgewebview2.exe` → `CreateProcessInternalW failed: status 0xc000007b` (ARM64 image) → NRE → FailFast, `rc=69` |
| `wa_battery.sh 8 60` | 1/8 `rc=124`, 7/8 `rc=69` (env chain fires ~t=40 s at battery timing; death = spawn failure chain), `fatal=0` in all 8 |
| app binaries after all runs | `CoreMessagingXP 24da83be`, `dwmcorei 9353066e`, `Microsoft.ui.xaml 9a42e19d`, `WebView2Loader 4cd3ffd5`, `Microsoft.Web.WebView2.Core 799b8c69`, `EmbeddedBrowserWebView 4c0d799a` — PRISTINE |

Screenshot `wa_locator_after.png`: **black** (295 B uniform root capture at t=105 s; the XAML
window is not mapped; the only mapped top-level is Wine's per-process desktop window).

Next gaps (ordered): (1) obtain the **x64** runtime payload — the offline package (x64
`Microsoft.EdgeWebView2Runtime` MSIX / offline installer's `msedge_wv2_x64.cab`, not this
bootstrapper) — and replace the ARM64 root of `Application\157.0.4341.16` (EBWebView\x64\
EmbeddedBrowserWebView.dll can stay); (2) then the browser child tree speaks Chromium's
named-platform-channel: the browser side calls `CreateNamedPipeW`/`ConnectNamedPipe` +
`GetNamedPipeClientProcessId`/`GetNamedPipeClientSessionId` (the 3f84f522d attrs path is the
wire to watch; the ImpersonateNamedPipeClient 16-failure pair only matters if the x64 browser
exercises it); (3) controller creation on the XAML window hwnd and `Navigate` remain gated by
the XAML activation gap (window created, never shown); (4) battery variance: env-chain fires
t≈40 s in battery timing vs t≈140 s in gate timing — worth pinning the timing source when the
x64 payload lands.

## The x64 WebView2 payload lands: official fixed-version cab, the Chromium tree runs under Wine, and the new wall is the managed completion path

**Payload source (option b/c of the recipe, resolved).** The x64 *Evergreen standalone*
installer `MicrosoftEdgeWebView2RuntimeInstallerX64.exe` (fwlink 2124701, 214 823 632 B) is
**unusable offline**: its only payload resource `.rsrc/0/B/102/0` is 214 608 444 B of
high-entropy data, `crsx`/`Cr24`/zstd-magic/gzip-magic/`msedge_wv2`/`http`/GUID-regex all
absent from the whole stream — the CMS payload is encrypted end to end, key inside the
closed-source updater, so there is no manifest to read. Ring probing the fwlinks gives
2124700 -> `MicrosoftWebView2Runtime_157.0.4341.16_Beta.msix` (299 637 224 B, AppxManifest
`ProcessorArchitecture="arm64"` — the package already deployed, arch-pinned),
2124701 -> the x64 bootstrapper above, 2124703 -> `MicrosoftEdgeWebview2Setup.exe` (1.8 MB
online bootstrapper); no x64 MSIX exists in that id set. What works is the **Fixed Version
channel** on the developer page, whose `filestreamingservice` GUID URLs are embedded in the
page JSON (escaped `\u002F`):

```
https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/074c0930-36ad-4756-9d04-dfe0e503bd94/Microsoft.WebView2.FixedVersionRuntime.156.0.4314.42.x64.cab
310 135 141 B, sha256 cb4612ce79a62a0be094301a72f73cf91bb250f5e83dc51886f66a083fc0694f
```

`cabextract` gives the ready-made runtime dir (674 MB, no installer, no MSIX):
`msedgewebview2.exe` 4 911 432 B machine **0x8664** (sha256[:8] 337a496f),
`msedge.dll` 355 157 320 B **0x8664** (e30fdd5b),
`EBWebView\x64\EmbeddedBrowserWebView.dll` 9 079 104 B **0x8664** (c105bb00). Provenance
cross-check: the community NuGet `WebView2.Runtime.X64/156.0.4314.42` ships byte-identical
copies of every file it carries (`msedgewebview2.exe`, `msedge_elf.dll`, the x64 client dll)
but **omits `msedge.dll`** — that package alone cannot run a browser, the cab can.
Deployed as a *self-consistent* pair at
`C:\Program Files (x86)\Microsoft\EdgeWebView\Application\156.0.4314.42`
(`/home/olge/.wine-alpc-test/drive_c/...`), the ARM64 root of `Application\157.0.4341.16`
(`msedgewebview2.exe` 0xAA64, `msedge.dll` 335 828 808 B 0xAA64) moved out to
`/tmp/wv2arm64bak`, its `EBWebView\` subtree kept. Registry recipe
`/home/olge/wa-work/envprep_wv2_x64.reg` = the df7056c9 acceptance test with
`EBWebView`=`...\Application\156.0.4314.42`, `pv`=156.0.4314.42 (HKLM+HKCU
`ClientState\{F3017226-...}`, `Clients\{56EB18F8-...}` `location`+`pv`), so the *client* dll
that the locator loads and the *browser* it spawns are the same 156 build — a 157 client
dll against a 156 browser would be a version-mismatched pair.

**The browser tree runs.** `NtCreateUserProcess` of the x64 `msedgewebview2.exe` succeeds —
no more `0xc000007b`; `+process` (the `+exec` channel **does not exist** at tip: every
`exec_total=0` in this project's history is a dead channel, `ntdll/unix/process.c` traces
under `+process`) shows `CreateProcessInternalW ... --type=utility --utility-sub-type=net…`
and the children: `--embedded-browser-webview=1 --webview-exe-name=WhatsApp.Root.exe
--webview-exe-version=1.0.0 --user-data-dir=…`, `--type=crashpad-handler`,
`--type=utility --utility-sub-type=network`, `--type=utility --utility-sub-type=storage`,
`--type=gpu-process` — a 5-process Chromium tree (transient; it disappears with the host,
Chromium watches the host process handle, so a `ps` snapshot at t=70 shows 0). The browser
process loads `msedge.dll` (355 MB) + `msedge_elf.dll`, creates its
`Chrome_MessageWindow`/`Chrome_WidgetWin_0`, and drives the profile for real:
`C:\users\olge\AppData\Local\WhatsApp\LocalCache\EBWebView\Default\…`
(“Site Characteristics Database”, `Subresource Filter`, `ReplaceFileW`). Zero spawn
failures, zero `named_pipe`-channel errors (client `\\.\pipe\LOCAL\mojo.32.436.<nonce>`,
browser side `mojo.440.600.<nonce>` ×3, and the app opens the browser's pipe names).

**App side advances to the controller, then dies in managed code.** The 156 client dll
loads in the app; the app creates `WEBVIEW2_TEMP_PARENT` “Webview2 Temporary Parent”,
`Chrome_WidgetWin_0` and `Microsoft.UI.Content.DesktopChildSiteBridge` in its own process —
the `CreateCoreWebView2Controller` path is reached (it never was with the ARM64 payload).
No `--type=renderer` child appears: no frame is ever created, so nothing loads. The
completion never lands: every env chain ends in a .NET FailFast storm (3 × `e0464645`,
33 × `RaiseFailFastException`, `rc=69`), with one
`RoOriginateLanguageException 0x80004002 "Specified cast is not valid."` in the window
(the app's `CoreWebView2.Profile` cast: applog history
`ZoomBrowserExtension/AddBrowserExtensionToWV > Failed to get CoreWebView2.Profile`).
`Navigate` is never reached — `applog.txt` contains **0** `navigat*` lines ever.

| metric | value |
| --- | --- |
| `wa_gate.sh 150` (x64) | `rc=69 DIED`, lines=210 074, `fatal=0`, `ff=33`, `e0464645=3`, `originate=4`, `orig_80070002=0`, `createdwins=54`, `xamlwin=1`, `edge=244`, `ebwebview=26`, `edge_procs(t=70)=0` |
| tree probe 90 s (`+process`) | `rc=69`, lines=137 439, `ff=33`, `e0464645=3`; child kinds: crashpad 2, network utility 2, storage utility 2, gpu-process 2, renderer 0; `NtCreateUserProcess` edge lines 10, `CreateProcessInternalW` edge 5, spawn failures 0 |
| `wa_battery.sh 8 60` | 1/8 `rc=124 ALIVE` (`edge=0`, chain not fired) + 7/8 `rc=69` each with `edge=2` (chain fired ⇒ death), `fatal=0` in all 8 — same rc distribution as the ARM64 baseline, so the x64 payload costs no stability; every death now has the chain firing |
| `wa_shot_run.sh 130` | `rc=69`, `ff=48`, `wins_created=54`, x windows = 10 (all “Default IME” 1×1 / 160×20), screenshot 295 B |
| screenshot `wa_x64_after.png` | **blank/black** — uniform 295 B root capture at t=105 s: no QR, no web content, no mapped app window |
| app binaries after all runs | `CoreMessagingXP 24da83be`, `dwmcorei 9353066e`, `Microsoft.ui.xaml 9a42e19d`, `WebView2Loader 4cd3ffd5`, `Microsoft.Web.WebView2.Core 799b8c69`, app-bundled `EmbeddedBrowserWebView 4c0d799a` — PRISTINE, zero app binary patching |
| Wine source | unchanged this session: process creation, named pipes, mojo bootstrap and the profile I/O all work; no gap on the spawn path was worth patching |

Next gaps (ordered): (1) the completion of the environment/controller round-trip —
identify the managed exception behind the `e0464645` storm (minidump route is closed:
`DOTNET_DbgEnableMiniDump` is a Linux-only coreclr knob, no dump is produced on Wine; the
practical route is `winedbg` stopped on `e0464645` reading `ExceptionInformation[0]`, the
managed exception object, then its `MethodTable` name); (2) the `0x80004002`
`CoreWebView2.Profile` cast the app makes (ZoomBrowserExtension) — check
`ICoreWebView2Environment12`/`ICoreWebView2Profile` on the 156 client against the app's
`WebView2Loader`/`Microsoft.Web.WebView2.Core` expectation; (3) renderer start
(`--type=renderer`) is untested — it is gated by a frame existing, which needs the
controller completion; (4) XAML `Activate()`/`ShowWindow` stays unreached (no top-level
window maps), so step-3 of the plan is now downstream of (1), not of its own gap.

## The FailFast decoded: it is CoreMessagingXP's own out-of-memory fail-fast, not a managed exception

Tip `7e83b8a56` + x64 WebView2 `156.0.4314.42`. Route used: `gdb` attach on the Linux pid with
python breakpoints on Wine-side exports (`ntdll!RtlRaiseException`, `combase!RoOriginateLanguageException`,
`kernelbase!RaiseFailFastException`, `ucrtbase!_callnewh/_expand/fwprintf/fputws/fputs/fwrite`) with a
per-run `/proc/<pid>/maps` symbolization, `+seh,+combase,+messaging,+storage,+wintypes` traces, and one
temporary Wine-side `WARN` in `ntdll` `RtlAllocateHeap`/`RtlReAllocateHeap` (removed; the committed tree
contains no probe). Static reading of `CoreMessagingXP.dll` (objdump) closed what the debugger could not.

**The "storm" is one event.** `ff=33` per run is Wine's own dump of a *single*
`RaiseFailFastException` call (32 backtrace frames + the `record:` line); `e0464645=3` is that same
event (record line, `NtRaiseException` line, one stack word). The record: `code=0xE0464645`,
`flags=0`, `NumberParameters=0`, `ExceptionAddress = CoreMessagingXP RVA 0x9399`; arguments at the
entry are `rcx/rdi` = a stack local record, `rdx=0`, `r8=0`, and `rax = kernelbase!RaiseFailFastException`
— i.e. the callee pointer was *resolved at runtime*. `params=0` ⇒ there is no stowed exception object,
so the `ExceptionInformation[0]` → `MethodTable` route is closed by construction.

**Who calls it** (objdump, `CoreMessagingXP.dll` `24da83be…`, RVA `0x936c`–`0x93ac`): read a global
callback (`0x1800e5150`), call it, then `OutputDebugStringW(L"Con::Process::NotifyOutOfMemory().\r\n")`
(string at RVA `0xb88c0`, import IAT `0xb5110`), then `GetModuleHandleExW(L"api-ms-win-core-errorhandling-l1-1-3.dll", 0, 0)`
(string at RVA `0xb8910`) and the dynamic resolution of `RaiseFailFastException` (also imported at IAT
`0xb4f88`), called as `RaiseFailFastException(&record, NULL, 0)`. The record's `ExceptionAddress`
`0x9399` is the instruction right after the `OutputDebugStringW` call at `0x938d` (its 5-byte call
padding `0x9394…0x9398`). So the fatal event is **the app's CoreMessagingXP process-out-of-memory
fail-fast on the UI thread (`tid=0x24`)**; its captured backtrace is unusable (`0xa8fd` lands mid-
instruction ⇒ stale stack values, not return addresses).

**The managed exceptions in the window are a different, handled population.** Every .NET raise is
`ntdll!RtlRaiseException` with `code=0xE0434352`, `info[0]=HRESULT`: 2× `0x80131502`
(`COR_E_ARGUMENTOUTOFRANGE`) and 2× `0x80004002` (`COR_E_INVALIDCAST`, message `L"Specified cast is
not valid."` — the `RoOriginateLanguageException` line). All four are on **worker** threads
(`0160/0174/01b0`, `tid` from the TEB at `gs_base+0x48`) and all four are **handled** (`RtlUnwindEx`
follows). The single `0x80004002` origination belongs to that handled cast, not to the death;
`RoOriginateLanguageException` carries the .NET-side message string (Wine's `combase` does not
synthesize it), which is what identifies the cast type.

**Wine is not the allocator that fails.** Temporary probe over a full run: `RtlAllocateHeap` failures
**0**; `RtlReAllocateHeap` `HEAP_REALLOC_IN_PLACE_ONLY` `STATUS_NO_MEMORY` **18**, all on `tid=00b4`,
all for `0x330/0x364/0x36a`, all with an in-process fallback; `ntdll`'s own
`out of memory for allocation` ERR never fires ⇒ no `NtAllocateVirtualMemory` failure. Process
footprint at death: `VmSize 271 GB` (Chromium V8 sandbox at `0x150000000000`, a 256 GB reserve, 32/16 GB
reserves) with `VmRSS 195 MB`, `max_map_count 1048576`, no `RLIMIT_AS`, `overcommit_memory=0` — the
reservations all succeed. `ucrtbase!_callnewh`/`_expand` are **never called**, so the new-handler that
CoreMessagingXP installs runs from its statically linked CRT. The event is timing-sensitive: with `gdb`
attached it moves out of the 150–210 s window (2 runs `rc=124 ALIVE`, `ff=0`).

**What the app was doing when it died** (UI thread, last trace lines): the tray-icon context menu —
`H.NotifyIcon.WinUI` + `H.NotifyIcon.dll` load, `Microsoft.UI.Xaml.Media.Imaging.BitmapImage`,
`MenuFlyout`, `Style`, `Setter`, `MenuFlyoutItem`, `FrameworkElement`, `UIElement`,
`Windows.Foundation.Uri`, `Windows.Storage.StorageFile` activations, `System.Drawing.Common`,
`System.Private.Windows.Core`, then the `H.NotifyIcon_*` window + `tooltips_class32` +
`__wine_tray_icon` creation.

**Fixes landed** (`346331f45`, `31e2ed6ad` on `ultimate`):
`combase`: real WinRT restricted error info — an `IRestrictedErrorInfo` object (v1 IID `82ba7092…`
and v2 `82ba70a5…`, v2 adding `ResolveReference`) stored per thread, `RoOriginate*` now originate and
store, `SetRestrictedErrorInfo`/`GetRestrictedErrorInfo` store/take, `RoSet/GetErrorReportingFlags`
keep the value. Previously the stubs returned `FALSE`/`S_FALSE`+`NULL` and threw the record away, so
the WinRT↔.NET error path ran blind and every `GetRestrictedErrorInfo` consumer saw "no error info".
`kernelbase`: `CreateMemoryResourceNotification` opened `\KernelObjects\LowMemoryCondition`, an event
Wine never creates, so it *always* failed; `QueryMemoryResourceNotification` left `*state`
uninitialized on its failure path. Now it hands out a private event in the healthy-machine state (low
memory not signalled, high memory signalled) and writes `*state` up front. Observed effect: the app's
UI-thread `CreateMemoryResourceNotification` (the .NET GC's pressure handle) now succeeds (`-> 0xE0`)
instead of failing.

| metric | before (`7e83b8a56`) | after (`31e2ed6ad`) |
| --- | --- | --- |
| `wa_gate.sh 150` | `rc=69`, `ff=33`, `e0464645=3`, `originate=6` (`2×80070002`, `3×80040111`, `1×80004002`), `createdwins=42`, `xamlwin=1` | `rc=69`, `ff=33`, `e0464645=3`, `originate=4` (`3×80040111`, `1×80004002`), `orig_80070002=0`, `createdwins=54`, `xamlwin=1`, `edge=244`, `ebwebview=26` |
| FailFast record | `e0464645`/`npar=0`/`CoreMessagingXP+0x9399` | identical (the OOM fail-fast is not the error-info path) |
| `Navigate` in `applog.txt` | 0 | 0 |
| renderer children (`--type=renderer`) | 0 | 0 |
| XAML window mapped | no (all top-levels 1×1 / 160×20) | no — screenshot `wa_exc_after.png` (t=55 s) 295 B **blank/black** |
| app binaries | pristine | `CoreMessagingXP 24da83be`, `dwmcorei 9353066e`, `Microsoft.ui.xaml 9a42e19d`, `WebView2Loader 4cd3ffd5`, `Microsoft.Web.WebView2.Core 799b8c69`, app-bundled `EmbeddedBrowserWebView 4c0d799a` — PRISTINE (sha256 prefixes), zero app patching |

Method limits worth keeping: breakpoints inside the app's native PE DLLs never fire (their mapped
pages do not accept the `int3`; breakpoints in Wine-built modules fire reliably — the same probe
captured `kernelbase`/`ntdll`/`combase` hits in the same session), so app-side sites are reachable only
by static reading; a `call fflush(0)` in the inferior deadlocks the process (do not repeat); coreclr's
`Unhandled exception.` report is never written (`fwprintf/fputws/fputs/fwrite` hooks saw nothing), so
no managed text is available for the fatal event; `DOTNET_DbgEnableMiniDump` remains Linux-only.

Next gaps (ordered): (1) the OOM fail-fast trigger — Wine-side allocators are clean, so the size and
the caller live in the app's static-CRT new-handler; the only remaining handle is to answer the
*prerequisite* the app is failing to allocate for: the tray/menu path above is where it dies, and the
`Windows.Foundation.Uri` activation with `IID IUriRuntimeClass` (`00000035-0000-0000-c000-000000000046`)
is answered `E_NOINTERFACE` by `wintypes`' Uri **factory** (`uri_factory_QueryInterface` answers
`IUriRuntimeClassFactory`/`IActivationFactory` only) — Windows's class factory also answers the class's
default interface; (2) `IUriRuntimeClass`/`IStorageFile` statics `GetIids`/`GetRuntimeClassName` stubs
in `wintypes`/`windows.storage` (the CsWinRT projection probes both); (3) the `0x80004002`
`CoreWebView2.Profile` cast — the app's own `ZoomBrowserExtension` handler catches it and logs
`Failed to get CoreWebView2.Profile` (last seen 11:44–11:52 runs, i.e. before the current gate), so it
is downstream of the death, not the cause; (4) renderer start and XAML `Activate()` remain gated by (1).

## The prerequisite chain lands: factory default-interface QI + real WinRT metadata; the wall stays CoreMessagingXP NotifyOutOfMemory (2026-10-26)

Tip chain `31e2ed6ad` → `164037aa5` (six commits, pushed to `ultimate`):
`5336b08c2` wintypes, `88f8048bc` windows.storage, `a3283f404` windows.networking.connectivity,
`cdb3de48f` windows.globalization, `ecd3514d9` windows.ui, `164037aa5` windows.applicationmodel.

**The measured E_NOINTERFACE is gone.** Windows aggregates the runtime class *default* interface (and the
implemented interfaces) on the class **factory** object; the CsWinRT projection QIs the factory for the
class IIDs at type registration. `wintypes`' Uri factory answered `IUriRuntimeClassFactory`/
`IActivationFactory` only, so `RoGetActivationFactory("Windows.Foundation.Uri") +
QI(IID_IUriRuntimeClass 9E363B3D-…)` returned `E_NOINTERFACE`. The factories now answer the default
interface — Uri (`IUriRuntimeClass`, `IUriRuntimeClassWithAbsoluteCanonicalUri`, `IStringable`, the
instance also gains `IStringable` with real `ToString`), Buffer (`IBuffer`), DataWriter (`IDataWriter`),
PropertySet (`IPropertySet` marker + `IObservableMap`), StorageFile (`IStorageFile`), StorageFolder
(`IStorageFolder`), GeographicRegion (`IGeographicRegion`), CoreWindow/CoreCursor/UIViewSettings/
InputPane/UISettings/AccessibilitySettings, AppInfo (`IAppInfo`), Package (`IPackage`) — the factory is
not an instance: instance method calls return `E_FAIL`, QI/refcount/inspectable metadata are live.
`GetIids`/`GetRuntimeClassName`/`GetTrustLevel` on factories and instances are real metadata (declared
interface IIDs from the widl headers; `GetIids` lists exactly the interfaces the object answers in QI).
`wintypes` DataWriter `ActivateInstance` returned `S_OK` with an **uninitialized** out-pointer — fixed
(`*instance=NULL`, `E_NOTIMPL`). All 13 `GetIids` stubs in `windows.networking.connectivity` and the
stub sets in the rest of the audited activation surface are filled. Audit notes: `twinapi.appcore`
factories answer *every* IID with shared dummy objects (permissive shim by design, app passes them —
tightening would regress, left as-is); `GetTrustedTypeMetadata` exists nowhere in Wine's surface
(grep-verified; not part of the widl `IInspectable`/factory contract).

**Gate 150 s (tip, pristine app binaries, `wa_gate.sh`):** `rc=69` — the app now dies at ~45 s (the
baseline survived to 150 s: the WebView2/mojo layer starts ~3× faster, so the same wall is reached
earlier). `ff=33` lines = **one** `RaiseFailFastException` event (30 backtrace frames + record lines,
same single-event signature), `e0464645=3` = that one record, `npar=0` unchanged. The failfast backtrace
resolves cleanly this run (maps at t=70): `kernelbase!RaiseFailFastException+0x5a92` ←
**CoreMessagingXP** `+0xb8fd/+0xb85c/+0x9cf4a/+0xa399` ← dispatch frames `+0x785e7…+0x12d72` ←
`user32+0x5a175/+0x45147` (window-proc dispatch) ← unresolvable managed frames ← `memfd:wine-mapping`
(coreclr JIT region) — i.e. the same `Con::Process::NotifyOutOfMemory` death on the UI thread (`tid=0x24`)
inside a CoreMessagingXP window-proc dispatch that crosses into managed code.
Originations (combase, now real): **4**, all in the handled population — 3× `0x80040111`
(REGDB_E_CLASSNOTREG, messages `L"WhatsAppNative.Voip.VoipFactory"`, `L"WhatsAppNative.Sqlite"`,
`L"WhatsAppNative.MiscCpp"` — the app's own native classes, app catches and logs) and 1× `0x80004002`
`L"Specified cast is not valid."` (the handled `CoreWebView2.Profile` cast). Progress: `msedgewebview2.exe`
now loads (browser + child processes in the loaddll trace), 4 `\\pipe\\LOCAL\\mojo.*` named pipes created,
`createdwins=54`, `xamlwin=1` (the XAML window is created). `Navigate` in applog: **0**; no
`--type=renderer` child. Screenshot `wa_uri_after.png` at t=35 s: **blank** (solid 1280×1024, 295 B).

**Battery 8×60:** `rc=69` ×8 (deterministic, the early death), `fatal=0` ×8, `edge=2` ×8.

**UI-thread trace at the death (`+module,+process`, last lines of tid 0024 before the failfast):**
`gdiplus.dll` loads → `NtUserCreateWindowEx` **`H.NotifyIcon_183e58c3-…`** (the tray icon window) →
`Shell32.dll` (app-local probe `c0000135`, system copy loads) → `ProcessPowerThrottlingState` stub →
`load_dll "api-ms-win-core-errorhandling-l1-1-2.dll"` → `kernelbase` → **RaiseFailFastException**.
The death is CoreMessagingXP's own out-of-memory fail-fast **during the tray-icon build dispatch**
(H.NotifyIcon + gdiplus + Shell32), exactly the population where it died before; the activation/QI
surface is no longer the trigger (Uri/StorageFile factories answer, the tray window is created).

**Next gaps (ordered):** (1) the `NotifyOutOfMemory` trigger: CoreMessagingXP reads a global callback
(`0x1800e5150`), calls it, then resolves `api-ms-win-core-errorhandling-l1-1-3.dll` and fail-fasts —
decode the callback (objdump): it is the app-side commit-limit probe; find the `NtQuerySystemInformation`
class it reads and the limit it compares against (Wine's commit accounting is the remaining suspect);
(2) `0x80040111` activations for `WhatsAppNative.Voip.VoipFactory`/`Sqlite`/`MiscCpp` (app-side COM
servers in the package dir — register their ActivatableClassIds via env-prep if the handlers stop being
benign); (3) the `CoreWebView2.Profile` cast (`0x80004002`) downstream; (4) `Navigate`/renderer remain
gated by (1).

### Correction: the first numbers above were the tip baseline; the fix binaries reproduce them (2026-10-26)

The gate/battery numbers in the section above were measured **before the fix binaries existed**:
builds 1–4 of the factory work all failed to compile (the exit codes read at the time were the
trailing `grep`'s, not `make`'s), so the prefix ran `31e2ed6ad` binaries. Compile defects found and
fixed (`e715eaeec` wintypes, `c21e87719` windows.storage, `3e8b19730` windows.networking.hostname,
`2a783e80a` windows.* follow-ups): `static const IID` arrays initialized from `extern const` `IID_`
symbols (not compile-time constants outside the initguid unit — now local copies), fill-regex sites
that swallowed the following function (one-line `GetIids` stubs — functions restored from tip),
`**trust_level` writes, `IID_IFileIoStatics` → `IID_IFileIOStatics`, typed vtbls in the hand-written
corewindow.c (now raw cast vtbls like the file's style), missing struct members where the substring
guard matched the statics interface. The sweep also extended the aggregation in-scope: the
`windows.storage/streams.c` factories (Buffer→`IBuffer`, InMemoryRandomAccessStream→
`IRandomAccessStream`, DataReader→`IDataReader`, DataWriter→`IDataWriter`,
RandomAccessStreamReference→`IRandomAccessStreamReference`) and the
`windows.networking.hostname` HostName factory (`IHostName` + real factory/instance metadata).

**Full build clean (`BUILD_RC=0`), prefix synced (7 DLLs updated, byte-verified).** The gate-2
(150 s) numbers with the fix binaries are **identical to the baseline**: `rc=69` (dies ~45 s),
one `RaiseFailFastException` event (`ff=33` lines, `e0464645=3`, `npar=0`), `originate=4` (same 3×
`WhatsAppNative.*` classnotreg + 1× Profile cast), `createdwins=54`, `xamlwin=1`, `edge=244`
lines/msedgewebview2 child loads, 4 mojo pipes, `Navigate=0`, no `--type=renderer`, screenshot
blank (295 B solid 1280×1024). Battery 8×60: `rc=69`×5 / `rc=124`×3, `fatal=0`×8, `edge=2`×5/`0`×3
(bimodal: runs that launch the WebView2 children die at ~45 s; the others live 60 s). The
measured E_NOINTERFACE path is **confirmed closed at runtime**: the gate trace shows
`RoGetActivationFactory(L"Windows.Foundation.Uri", {00000035-0000-0000-c000-000000000046})`
resolving through `wintypes.dll` (was: QI refused by the factory). The wall is therefore exactly
the task-4 branch: **the `NotifyOutOfMemory` fail-fast is independent of the activation surface**;
it fires inside the H.NotifyIcon/gdiplus/Shell32 tray-build dispatch (UI-thread `+module,+process`
tail: `gdiplus.dll` load → `H.NotifyIcon_…` window → `Shell32` → `api-ms-win-core-errorhandling-l1-1-2`
→ failfast). Next: decode the CoreMessagingXP global callback (`0x1800e5150`) — the app-side
commit-limit probe — and the `NtQuerySystemInformation` class/limit it compares (Wine's commit
accounting remains the suspect). Pristine app hashes end-verified unchanged
(24da83be/9353066e/9a42e19d/4cd3ffd5/799b8c69/4c0d799a).

## The OOM fail-fast decoded: it is ALPC PORT_MESSAGE type fidelity, not memory accounting (2026-10-26)

The task hypothesis (`NtQuerySystemInformation` commit-limit probe at the `[0x1800e5150]`
callback) is **dead on static decode**: that global is `Cn::Process::OutOfMemoryHandler`,
installed by `ConfigureNewContext` (CoreMessagingXP RVA `0xc486`), and its body
(RVA `0xcf70`) emits **only an ETW event** (`EventWriteTransfer(System_OutOfMemory)`, gated by
`CoreMessagingEnableBits & 2`) — it samples nothing. All 13 `NotifyOutOfMemory` call sites
(`0x936c`) are plain allocation-failure branches (`malloc`/`HeapAlloc`/`VirtualAlloc`/`LocalAlloc`
returning NULL); Wine allocators measured **zero failures** (TEMP ntdll heap probe: 0; VirtualAlloc
probe: only 6 V8-sandbox `addr=0x10000 size=0x7ffd0000` `STATUS_CONFLICTING_ADDRESSES` probes).
The CoreUI Buffering pool was dumped at the death instant via a Wine-side probe
(`g_MediumBlockPool` bitmap at module+0xe5640): **31/32 group-0 bits free, groups 1..31 untouched**
— no exhaustion. `TlsAlloc` never failed; `PostMessage/SendMessageCallbackW/RedrawWindow` never
failed (TEMP win32u probes: 0 hits).

The `wa_diag150` failfast bt symbolizes (module base `0x6ffffa6f0000`): record `addr` RVA `0xa399`
is inside a **generic Abandonment stub** (`0xa390`: `sub rsp; call CFlat::Abandonment::Fail`) — the
decompiler's `CreateCrossPartitionReverseConnectionPort$FastReturn$` label. The stack slots (raw
`failfast_stack` dump; the x64 unwinder stops at the stub because the app's dispatch region has no
`.pdata` entries) decode via `.pdata`/RVA to the real chain:
`UserAdapter::OnUserDispatch` (CoreMessaging private message `0x60`, the tray dispatch) →
`DrainCoreMessagingQueue` → `DeferredCallDispatcher::Callback_OnDispatch` →
`RegisteredWait::DoCallback` → `AlpcClientAdapter::Callback_ProcessIncoming` →
`AlpcConnection::Callback_ProcessIncoming` → `Callback_ProcessReceivedBuffer` →
**`Callback_HandleReceivedBuffer` (RVA `0x78340`) → stub → `Abandonment::Fail`**.

`Callback_HandleReceivedBuffer` dispatches on **`PORT_MESSAGE.u2.s2.Type`** (byte at +4):
`0` unsolicited→host vtbl+40, `1` reply, `2` cancel, `3` sendreceive, `4` datagram, `7` keepalive-ack,
`8`/`9`/`10`/`11` disable/dealloc/reconnect, `13` `HandleIncomingConnection`, everything else →
**`Abandonment::Fail`** (the `0x785e2` stub; `0x16`=22 → fatal). Wine's ALPC emulation
(`dlls/ntdll/unix/alpc.c`) stamped the **user-visible** `u2.s2.Type` with its *internal* wire values
(request=**1**, reply=**0x02**, datagram=**2**, connection-request=**10**, new-message=**0x10**):
the app read reply-as-cancel (the "RegisterConversation result 99" wall), request-as-reply, and
Wine's `0x10` wake-packet stamp as type **16 → fatal contract violation**, deterministically during
the H.NotifyIcon tray `WM_CREATE` dispatch (`call_window_proc` probe: last dispatches
`WM_GETMINMAXINFO→WM_NCCREATE→WM_NCCALCSIZE→WM_CREATE` on the `H.NotifyIcon_0114335d-…` window,
tid `0x24`).

**Fix (wine `91e2023d6` + `139d8693d`, pushed to github/ultimate):** stamp the Windows LPC values in
the user-visible PORT_MESSAGE (`LPCRequest 0`, `LPCReply 1`, `LPCDatagram 4`, `LPCNewConnection 7`);
the wineserver wire header is a separate field and keeps its internal stamps. Plus win32u
`NtUserRedrawWindow`: set last error on the two bare-`FALSE` region paths (Windows sets it; the
dispatcher reads `GetLastError()` after a failed RedrawWindow and Wine left stale values).

**Gate 150 s (clean, probes stripped):** the tray-dispatch death is **gone** — no UI-thread
failfast, no `NotifyOutOfMemory`, `H.NotifyIcon WM_CREATE` completes. New deterministic wall, 8/8
runs: worker tid `0138`, record `e0464645` **npar=2** (`param[0]=0xC00000B0 STATUS_PIPE_DISCONNECTED`)
at RVA **`0x853AA` = `AlpcServerConnection::CommitToSlurp` → `FailWithHR`** — the app's registrar
conversation handler returns DISCONNECTED (the RegisterConversation-result-99 wall surfacing as its
own fail-fast). Applog now reaches VoIP/WebLifecycleController/push-notification subsystems (far
past tray build). `rc=69`×8 (was bimodal 3/8 alive), `fatal=0`, `edge=0`, `createdwins=14`,
`xamlwin=0`, `Navigate`/renderer not reached (registrar wall is upstream of WebView2 launch).
Screenshot `wa_oom_after.png`: solid black 1280×1024 (app dead at ~50 s; no app window mapped).

**Tests:** `ntdll alpc.test` and `user32 win.test` **identical rc on tip vs fix builds** (pre-existing
failures; no regression). kernel32/kernelbase source-identical to tip. All TEMP probes removed
(`WINEPROBE` grep: 0 hits in the tree); build==source parity verified after the final sync.
**Pristine app hashes end-verified unchanged** (24da83be/9353066e/9a42e19d/4cd3ffd5/799b8c69/4c0d799a).

Next gaps: (a) the registrar conversation completing with DISCONNECTED at `CommitToSlurp` — the
app's own slurp-protocol state machine on Wine's emulated ALPC (the wake-packet round-trip now
delivers, so the conversation *content* is the suspect: the registrar request reply payload/status
the app's `RegisterConversation` host builds); (b) after that, XAML window map + WebView2
locator/renderer chain.

## Session 2026-10-24(b): the CommitToSlurp wall decoded — ALPC connect-by-reply, MessageId offsets, self-host marker

**Slurp anatomy (IDA `CoreMessagingXP.dll`):** `AlpcServerConnection::CommitToSlurp` (RVA `0x853AA`)
is the server side of the LPC connect dance: the registrar server thread (`tid 0138`) slurps the
connection request notification on the **listening** port, then answers it with a zero-payload
`NtAlpcSendWaitReceivePort(port, 0x10000 /*REPLY*/, {DataLength 40, TotalLength 80}, attrs=0)` —
MessageId 0, no context attribute. `NtAlpcAcceptConnectPort` is **never called** (request trace:
`accept_alpc_connection` count 0): the kernel is expected to complete the connect from the reply
and route the server's later id-less replies by the connection's outstanding request.

**Desync found (three layers, all in `server/alpc.c`):**
1. Connect completion existed only on the explicit accept request → the provider's connect stayed
   `PENDING`; the engine's connect reply hit the empty-connections fallback → `STATUS_PIPE_DISCONNECTED`
   (`alpc.c` set_error) → `FailWithHR` at `0x853AA` (the deterministic 8/8 wall).
2. The server read the sender's PORT_MESSAGE **MessageId at +0x18** — the 32-bit offset. The x64
   `ALPC_PORT_MESSAGE` carries it at **+0x20** (Type at **+4**, right behind DataLength/TotalLength,
   which is also where ntdll and the app stamp it). Every reply therefore correlated to id 0: the
   armed pair receive (request id N) never took the reply, routed-request replies carried ctx id 0
   (the historic "RegisterConversation result 99" family), and the reply was consumed by the bare
   receive as a verb-0 notification.
3. No outstanding-request tracking: a REPLY-flag send with id 0 (the slurp commit, the per-request
   reply) must be answered by the connection's slurped request id.

**Fix (wine `bab0b58fb`, pushed to github/ultimate):** complete pending connections from the
REPLY-flag send on the listening port (`complete_alpc_connection`, shared with the accept route,
port context from the reply's CONTEXT attribute); read Type/MessageId at the layout offsets
(+4 / `4*ptr`); track the per-connection outstanding request id (client connection port, shared
with its reserve handle) and stamp id-less replies with it, delivering the id in the delivered
PORT_MESSAGE as the kernel does; stamp the forced type at +4 (and `ALPC_LPC_REPLY` = 1, the
Windows LPCReply value, replacing the 0x02 that was being written into the TotalLength slot).

**Provider side (wine `d526f5882`):** the engine records the client's partition GUID from the
24-byte connect port context; a client without the engine's self-host marker (the `0x20` value the
server echoes at +8 of every connect reply payload, carried with the Cn engine GUID tail exactly
as the engine's own in-process clients send it) is classified **cross-partition**:
`MessageSession::CreateConnection` → `CreateCrossPartitionReverseConnectionPort` →
`Abandonment::Fail` (RVA `0xa399`, `params=0`) — the new wall, hit by the provider's own
connection. The provider now mirrors the marker, echoes the join requester's own partition GUID
into the join reply's peer RoutingInfo (zero GUID = same cross-partition branch), and keeps a
single outstanding exchange (the engine's `AlpcConnection::DoReply` fail-fasts replying with >1
request buffers outstanding).

**Numbers:** the old wall is **gone**: 0/24 battery+gate runs show `e0464645@0x853AA`. New
deterministic wall `e0464645@0xa399` (XAML self-host connection: DoReply count>1 / cross-partition
reverse-port machinery, a Wine kernel-emulation gap, not the provider conversation): battery 8×60
all `rc=69` at `0xa399` (~15–30 s); gate 150 s bimodal — one run **ALIVE_150s with ff=0,
e0464645=0** (provider conversation fully healthy, tray dispatch + VoIP/WebLifecycle/push applog,
`xwindows` up to 139), others die at the `0xa399` wall. `fatal=0`, `originate=0`, `edge=0`,
`wv2loader=0`, `createdwins=14`, `xamlwin=0` (XAML window map / WebView2 chain sit behind the new
wall). Screenshot `wa_slurp_after.png`: solid black 1280×1024 (no app window).

**Tests/probes:** relay trace (behind `WINE_ALPC_RELAY_TRACE`) + wineserver `-d 1` request trace
used for the decode (in-tree, env-gated, kept); all ad-hoc probes removed (`PROBE`/reply-trace
greps: 0 hits in the diff); build==source parity (BUILD_RC=0, 0 warnings in `server/alpc.c`;
`registrar.c` warnings are the file's pre-existing `%u` format style). **Pristine app hashes
end-verified** (24da83be/9353066e/9a42e19d/4cd3ffd5/799b8c69/4c0d799a), zero app binary patching.

**Next gaps:** (a) `0xa399`: the XAML-side self-host connection — `AlpcConnection::DoReply`'s
multi-buffer reply contract (Windows ALPC multi-buffer/posted-buffer flow semantics) and
`CreateCrossPartitionReverseConnectionPort`'s port machinery (the engine's own in-process clients
take this branch on Windows; it needs the posted-buffer + reverse-port emulation in
`server/alpc.c`); (b) then XAML window map + WebView2 locator/renderer chain.
