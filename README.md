# wine-ultimate — WinUI 3 / Windows App SDK & Performance Stack for Wine

> **Honest disclaimer:** `wine-ultimate` is an experimental AI-assisted Wine fork ("AI-slop" by traditional Wine upstream standards — large, rapidly-iterated C/ASM subsystems built and verified with automated profiling, Wine test suites, and live WinUI 3 / UWP app gates, not upstream-reviewed patches). It is **not** clean upstream Wine and should be evaluated by its measured benchmarks, Wine test results, and clean-room legality below.
>
> **Goals & Direction:** Our goals are getting real UWP / WinUI 3 apps working, increasing Windows compatibility, and making Wine faster — which has led to aggressive and sometimes questionable low-level designs (~880+ commits on top of upstream Wine 11.10: custom shared-memory arenas, inline ASM fast-paths, and rapid WinRT surface stubs). Most architectural directions and priorities were human-advised by the repository author (not a C developer), with AI agents handling C/ASM implementation, profiling, and test verification.
>
> **Models used:** `gemini-4-hydrogen`, `gemini-4-argon`, `gemini-3.8-flash`, and a local custom `strata` model (`heretic-v2-iq4_xs-plus`, `IQ4_XS` 4-bit quantization with `k8v4` KV cache).

---

## Why `wine-ultimate` is Legal (Clean-Room Builtin Implementation)

`wine-ultimate` was implemented as an explicit clean-room alternative to [designgears/wine-uwp](https://github.com/designgears/wine-uwp) — while `wine-uwp` harvests Microsoft's own system frameworks (`Windows.UI.Xaml.dll`, `CoreMessaging.dll`, app-model DLLs) locally from a Windows 11 evaluation ISO on the user's machine, we reject relying on Microsoft ISO system DLLs and instead reimplement the WinRT/XAML/DComp stack as clean-room Wine builtins.

1. **Zero Microsoft binaries redistributed**: This repository contains only LGPL-2.1+ Wine C/header/IDL source code (`dlls/`, `server/`, `include/`, `loader/`). No Windows DLLs, `.winmd` files, or proprietary Microsoft binaries are committed, linked, or shipped.
2. **Clean-room API/ABI reimplementation**: Every WinRT/UWP/WinUI/DirectComposition interface (`windows.ui.xaml`, `windows.ui.composition`, `dcomp`, `windows.storage`, `windows.web`, `windows.applicationmodel`, `appxdeploymentclient`, `cabinet` MSZIP) is implemented from scratch in C against public Windows SDK IDL/WinMD interface contracts, documented COM vtable slot layouts, and observed runtime error/query behavior (`QueryInterface`, `RoGetActivationFactory`).
3. **User-supplied app packages at runtime**: Any UWP/WinUI 3/DesktopBridge app (`Calculator`, `Notepad`, `Winget`, `Clock`, `Store`, `Paint`, `WhatsApp`) is supplied by the user's own local prefix/MSIX packages at runtime; `AppxManifest.xml` `<InProcessServer>` resolution (`combase`) simply loads the user's existing package DLLs the same way Windows `combase.dll` does.

---

## Verified App Status (`ultimate-sync`, Nov 2026)

| App | Status | Verified Behavior & Evidence |
|---|---|---|
| **Windows Calculator** (`CalculatorApp.exe`) | **PASS** | Full GPU SDF + Glyph Atlas + AOT XBF (`7 + 8 = 15` via mouse clicks & keyboard, Hamburger button `uwp_invoke_button`); cold TTW cut `1,130 ms → 625–669 ms` (`396–408 ms` warm server), `2560×1376` crisp on Xorg `:1` |
| **Windows Notepad** (`Notepad.exe`) | **PASS (Xvfb)** | `TabView`, `MenuBar` (`File / Edit / View`), `RichEditD2DPT` typing (`*Hello Wine 123 - Notepad`), `Ctrl+S` save/reopen, status bar (`Ln 0, Col 15`); `923 ms` cold TTW (`DISPLAY=:1` Xe/KWin `WS_CHILD` offscreen GPU→BitBlt fix in progress) |
| **Windows Package Manager (`winget.exe`)** | **PASS** | `winget source update` (`EXIT=0`, downloads & extracts `3.82 MB` `source2.msix` via `IAppxFactory` + `windows.web` + `cabinet` MSZIP), `winget search 7zip` (`EXIT=0`), `winget install 7zip.7zip --silent` (`7-Zip 26.06 (x64)` runs), `echo y \| winget list 7zip` |
| **WhatsApp Desktop** (`WhatsApp.Root.exe`) | **PASS (Onboarding)** | Reaches native `"Welcome to WhatsApp"` / `"Log in"` screen (`28/28` host bridges `S_OK`, `XWID map`, alive 30–90s); `wineserver` message-pump storm cut `270,970 → 45,742` RPCs (`−83.1%`) |
| **Microsoft Clock** (`Time.exe`) | **PARTIAL** | `NavigationView` (`Clock / Alarm / Timer / Watch`) + world-clock ring render (`instances=12`, `1272×990`); inner digits/controls incomplete |
| **Microsoft Store** (`WinStore.App.exe`) | **PARTIAL** | `SplashPage` alive 35s (`WindowsIntegrityPolicy` + `ICompositor6` cleared); blocked before `AppShell` on `IReferenceTracker` (`{11d3b13a}`) & `WinStore.UX.Controls.Card` (`0x80004003`) |
| **Microsoft Paint** (`mspaint.exe`) | **IN PROGRESS** | `NamedPolicy`, `LoggingOptions`, `ParseApplicationUserModelId`, WinUI 3 `AppxManifest` routing cleared; `1280×1024` frame maps live, swapchain present under investigation |

---

## Architecture & Subsystem Stack (`ultimate-sync`)

### 1. Server Shared-Memory Arenas (`<server_dir>/*_shm` & `pe_reloc_*.shm`)
- **`reg_shm`** (`server/registry.c`, `dlls/ntdll/unix/registry.c`): Read-only `MAP_SHARED` snapshot of registry keys, values, handle map, negative opens, and per-key enumeration snapshots (`NtEnumerateKey` / `NtEnumerateValueKey` `e6b4d0dee`).
- **`alpc_shm`** (`server/alpc.c`, `dlls/ntdll/unix/alpc.c`): Shared `PROT_READ | PROT_WRITE` arena (`512` ports × `16` ring slots) for zero-RPC ALPC query and datagram fast-paths.
- **`obj_shm`** (`server/handle.c`, `dlls/ntdll/unix/server.c`): Read-only `MAP_SHARED` handle/object table with seqlocks for zero-RPC `NtQueryObject` / `NtCompareObjects`.
- **`iocp_shm` + `io_uring`** (`server/completion.c`, `dlls/ntdll/unix/{sync.c,io_uring.h,file.c}`): Shared `256 × 256` IOCP completion ring fed directly by Linux `io_uring` CQEs (`aa4ebe048`).
- **`msg_shm`** (`server/queue.c`, `dlls/win32u/message.c`): Per-thread `64`-slot `MAP_SHARED` posted-message ring + masked `get_queue_status()` wait-mask fix (`f0b218850`).
- **`win_tree_shm`** (`server/window.c`, `dlls/win32u/window.c`): Read-only `4,096`-node `MAP_SHARED` window tree for zero-RPC hierarchy and hit-test queries.
- **`pipe_shm`** (`server/named_pipe.c`, `dlls/ntdll/unix/file.c`): Per-pipe `64 KiB` `MAP_SHARED` bidirectional ring for `ncalrpc`/`ncacn_np`.
- **`pe_reloc_*.shm`** (`dlls/ntdll/unix/virtual.c`): Cross-process shared relocated read-only PE sections keyed by `(dev, ino, size, mtime, reloc_base)`.

### 2. Core `ntdll`, `kernelbase`, `win32u`, Networking & Media Fast-Paths
- **Client-local sync handles** (`dlls/ntdll/unix/sync.c`, `156aa7732`): `16,384`-slot `0x40000000`-tagged unnamed Event/Semaphore futex pool with automatic promotion via `server_promote_local_handles()`.
- **File & path caches** (`dlls/ntdll/unix/file.c`, `kernelbase/file.c`): `50 ms` `path_cache` lease, `2048`-slot negative xattr cache (`134f12d30`), `dos_drive_cache` + `fd_drive_cache` (`7153d8b32`), `dos_to_nt_path_fast()` stack path (`041e7ed13`), `wine_cached_unix_pid()` (`6ee043941`).
- **Virtual memory & loader** (`dlls/ntdll/{unix/virtual.c,loader.c,heap.c,signal_x86_64.c}`): Private `SEC_RESERVE` commit bitmap (`932e5874f`), `WINEPATH`/`PATH` directory-indexed DLL lookup (`04cfe956a`), TEB lock-free small-block heap magazine (`56044aa58`), PE-side x86-64 `NtContinue` fast-path (`905d71613`), direct `KUSER_SHARED_DATA` `GetSystemTimeAsFileTime` + opt-in `inline_patch` (`c65b684a4`).
- **Graphics, Font & Input** (`win32u`, `wined3d`, `winex11.drv`, `winebus.sys`, `mfplat`, `winegstreamer`): `256` striped `gdi_buckets` (`cbdb59190`), `512`-slot `TextExtent` cache, Fontconfig binary face cache (`880261c2c`), TLS `MakeCurrent` elision + split EGL init (`c76552d68`), `wined3d` `inline_cs` Vulkan ring (`e19fcbf0b`), Vulkan Video / VA-API zero-copy `MFPlat` (`dc869f41f`), lazy `SDL_Init` + `DisableUdevd=1` (`06a0e6792`), user-space `IOCTL_AFD_POLL` (`38d4494a0`).
- **COM / Networking / CRT** (`combase`, `oleaut32`, `secur32`, `dnsapi`, `iphlpapi`, `msvcrt`): `RoGetActivationFactory` 512-bucket cache + `AppxManifest.xml` `<InProcessServer>` auto-registration (`eb416adad`, `6a7739fac`), lockless `CoGetApartmentType`/GIT (`451124c94`), `TLBFuncIndex` (`14a2bcc59`), Schannel TLS tickets (`e7669c4db`), `DnsQueryEx` TTL (`e75b18d12`), `iphlpapi` 2s snapshot (`cbc9a873a`), AVX2/SSE2 UTF-8 + CRT (`a4e8ecfb3`).

### 3. Clean-Room WinRT / UWP / WinUI / Packaging Stack
- **`windows.ui.xaml`** (modularized in `6cf69d2c0`): XBF v2 parser + AOT `'XBFA'` disk cache, GPU-native instanced SDF + glyph atlas renderer (`gpu_renderer.c`), `NavigationView`, `MenuBar`, `TabView`, `RichEditD2DPT` / `CoreWebView2` host bridge, `TogglePaneButton` (`ed3dc824b`).
- **`windows.ui.composition` + `dcomp`**: `ICompositor5` / `ICompositor6`, `CompositionGeometricClip`, `RedirectVisual`, `ShapeVisual`, `BooleanKeyFrameAnimation` (`9193e7096`, `705a09d37`).
- **`appxdeploymentclient` + `windows.applicationmodel` + `windows.storage` + `windows.web` + `cabinet`**: ZIP64 `CLSID_AppxFactory` / `CLSID_AppxBundleFactory` + MSIX extraction (`07b2b2c8a`), generic `xmllite` `AppExtension` scanner (`df022d98f`), `HTTP` + `IBuffer` streams (`857544672`), `COMPRESS_ALGORITHM_MSZIP` compression & decompression (`2d1b558c5`).

---

## Measured Performance & Test Results (`ultimate-sync`)

- **WhatsApp `wineserver` Message-Pump Storm (`f0b218850`)**: `get_message` `124,158 → 42` (**−99.97%**), `set_queue_mask` `46,862 → 16` (**−99.97%**), `event_op` `41,412 → 22` (**−99.95%**); total startup RPCs `270,970 → 44,912` (**−83.4%**).
- **`reg_shm` Enumeration Snapshots (`e6b4d0dee`)**: `CalculatorApp.exe` `enum_key_value` `46,038 → 6,660` (**−85.5%**), total RPCs `88,460 → 50,992` (**−42%**), cold TTW `1,130 ms → 669 ms` (**−41%**).
- **Cold-Start Syscall Cuts (`06a0e6792`, `880261c2c`, `134f12d30`, `7153d8b32`)**: `openat` `77,098 → 15,306`, `newfstatat` `97,767 → 28,738`, `getxattr` `11,806 → 0`, `dosdevices/c:` `readlink` `496 → 14` (**−97%**).
- **PE-Side `NtContinue` Fast-Path (`905d71613`)**: `RaiseException + VEH` (`N=50,000`): `210.5 ns → 48.6 ns/exception` (**4.33× speedup**).
- **`GetSystemTimeAsFileTime` + Opt-In `inline_patch` (`c65b684a4`)**: `Tick64+Time+Heap` `66.39 ns → 3.31 ns/iter` (**20× faster** without IAT patching); `CalculatorApp.exe` cold start `-107 ms` (`731–734 ms → 625–626 ms`).
- **256 Striped `gdiobj` Bucket Locks (`cbdb59190`)**: 4-thread GDI throughput `0.69 M → 2.63 M iters/s` (**3.83× higher**).
- **Client-Side `SEC_RESERVE` Commit Bitmap (`932e5874f`)**: `WhatsApp` `add_mapping_committed_range` RPCs `3,009 → 0` (**−100%**).
- **`WINEPATH`/`PATH` DLL Directory Index (`04cfe956a`)**: `CalculatorApp.exe` `.dll` `c0000034` probes `130 → 67` (**−48.5%**); 50-round bench `10.74 ms → 6.11 ms` (**−43.1%**).
- **Wine Test Suites (0 regressions on modified lanes)**: `cabinet_test` **506/506** (`compressapi` 75/75, `fdi` 158/158, `extract` 273/273), `ntdll:thread` 67/67, `ntdll:time` 10,662/10,662, `kernel32:time` 2,358/2,358, `ntdll:loader` 70/70, `gdi32:bitmap` 1,549,034/1,549,034, `gdi32:dc` 1,657/1,657, `gdi32:gdiobj` 170/170, `ws2_32:sock` 0 failures.

---

## Build

Canonical branch: [`ultimate-sync`](../../tree/ultimate-sync) (upstream Wine 11.10 base + full stack as real commits; historical patch series live on `patches-archive`).

```bash
git clone -b ultimate-sync git@github.com:ZhymabekRoman/wine-ultimate.git
cd wine-ultimate
mkdir build && cd build
../configure --prefix=/usr/local/wine-ultimate --enable-archs=i386,x86_64
make -j"$(nproc)"
```
