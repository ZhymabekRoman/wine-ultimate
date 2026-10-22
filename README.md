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
