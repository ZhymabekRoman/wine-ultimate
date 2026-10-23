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
`FrameworkApplication::StartDesktop` fails on `UISettings`.

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

### 4. Prefix materialization

Wine 11 materializes builtin PE DLLs into the prefix (`drive_c/windows/system32`,
`syswow64`). A restored/skeleton prefix fails at `kernel32.dll` (`c0000135`);
repopulate from the build tree (`dlls/*/x86_64-windows/*.dll` → `system32`,
`dlls/*/i386-windows/*.dll` → `syswow64`) before first run.

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
