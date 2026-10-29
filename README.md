# FractureRecomp

<p align="center">
  <img src="banner.jpg" alt="Fracture Recomp" width="420">
</p>

An unofficial native recompilation of the Xbox 360 version of **Fracture**
(Day 1 Studios / LucasArts, 2008) for Windows, built on the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

The game's PowerPC code is translated to C++ ahead of time and compiled into a
regular Windows executable. Rendering goes through the SDK's emulated Xenos GPU
on Direct3D 12 or Vulkan.

> **Status: early.** The game boots and runs on the test machine (Windows 11,
> GeForce RTX 4080 SUPER). It has not been played through or tried on other
> hardware yet. See [Development](#development) for where this stands.

This repository contains **no game files and no code generated from them**.
The release downloads contain the recompiled program (`fracture.exe`) but no
game data: you need your own legally obtained Xbox 360 copy of Fracture, and
the program copies the game files from your disc image on its first start.

## Playing

### Requirements

- Windows 10 (version 1903 or later) or 11, x64, or a Steam Deck / Linux with
  Proton (not tested yet)
- A graphics card with Direct3D 12 or Vulkan
- A disc image (`.iso`) of the Xbox 360 release of Fracture. The supported
  executable is title ID `4C4107D4`, `default.xex` SHA-256
  `13fa6817089233d6d2b30adeca5c8bf74871ea28d0c620253aade6ee89557db2`; other
  releases are refused, because the recompiled code is made from that file.
- About 4 GB of free disk space for the game files

Nothing else: no Visual Studio, no Python, no runtime installers. The archive
carries the Visual C++ runtime it needs.

### First start

1. Download `FractureRecomp-v<version>-windows-x64.zip` from
   [Releases](https://github.com/ASarafanov/fracturerecomp/releases) and
   unpack it somewhere you can write to, for example
   `Documents\Games\FractureRecomp`. Not into `Program Files`: settings and
   logs are written next to `fracture.exe`.
2. Start `fracture.exe`. The program is not code-signed, so Windows may show
   "Windows protected your PC": click **More info**, then **Run anyway**.
3. The install screen opens. Choose your disc image with **Browse...** (or
   drop the `.iso` onto the window) and, if you like, another install folder
   with **Change...** (default: `game` next to `fracture.exe`), then
   **Install**. The image is checked first and only read, never changed.
   Copying takes a minute or two.
4. Press **Play**. Later starts go straight to the game; the `.iso` is no
   longer needed.

Already have the game files extracted (a folder with `default.xex` and
`resources`)? Use **Use extracted files...** instead, or drop that folder onto
the window. If the copy is interrupted (the window closed, the PC off), the
next start offers to continue where it stopped.

The screen works with the mouse, the keyboard (Tab or arrows, Enter) and a
controller (D-pad, A).

### Files

| Where | What |
| --- | --- |
| `game\` (or the folder you chose) | The game files from your disc |
| `settings.toml` next to `fracture.exe` | Your settings, and the install folder if it is not `game\` |
| `logs\` next to `fracture.exe` | Log files, useful for bug reports |
| `Documents\fracture` | Saves and the shader cache |

To update, unpack the new version over the old folder: the game files and
your settings stay.

### Steam Deck

Not tested yet. In Desktop Mode, unpack the archive (or copy a folder that
already has `game\`), add `fracture.exe` as a non-Steam game and turn on
Proton for it (Properties > Compatibility). The install screen's file chooser
works there too. Graphics API "Automatic" picks Vulkan under Proton.

### Antivirus

New, unsigned programs are sometimes flagged by antivirus heuristics. If yours
removes or blocks `fracture.exe` or its DLLs, whether to allow them (Windows
Security > Protection history) is your decision; the source is all here to
check or build yourself.

## Building from source

For development, or to make the program yourself instead of downloading it.

### Requirements

- Windows 10/11 x64
- Python 3.9 or newer
- Visual Studio 2022 Build Tools with the C++ workload (provides the MSVC
  libraries, the Windows SDK, CMake and Ninja)
- LLVM/Clang 20 or newer
- Git (setup downloads the ReXGlue SDK source with it)
- Your Fracture `.iso` (see above) and about 8 GB of free disk space, not
  counting the `.iso`

Install the build tools with:

```
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id LLVM.LLVM
winget install --id Git.Git
```

### Setup

Drag your `.iso` onto `setup.bat`, or run it from the repository folder (the
same command works in Command Prompt and PowerShell):

```
.\setup.bat path\to\Fracture.iso
```

Without an argument it uses the single `.iso` in the repository folder, or asks
you to pick one. The setup program:

1. checks that the image is the supported Fracture release;
2. extracts the game files into `game\` (about 4 GB);
3. downloads the ReXGlue SDK 0.10.0 source with Git (about 700 MB, checked
   against the release tag's commit) and builds the SDK with Direct3D 12 and
   Vulkan into `third_party\rexglue-sdk\`. The SDK's own Windows package has
   no Vulkan;
4. recompiles `default.xex` and builds `fracture.exe`;
5. writes `fracture.toml` next to `fracture.exe` so it finds `game\`.

Steps 3 and 4 take about 10 minutes on a 16-thread CPU the first time.

When started by double-click or from PowerShell, it waits for a key press at
the end. Running it again skips the steps that are already done. Pass
`--config release` or `--config debug` for another build type
(default: `relwithdebinfo`).

Then double-click `out\build\win-amd64-relwithdebinfo\fracture.exe`, or run
it from PowerShell (a quoted path needs `&` in front):

```
& .\out\build\win-amd64-relwithdebinfo\fracture.exe
```

Copying the whole repository folder, including `game\` and `out\`, to another
PC or a Steam Deck works too: the game finds `game\` again on its own, even
though `fracture.toml` still names the old location.

### Making a release

```
python tools/package_release.py --version 0.3.0
```

builds the `win-amd64-release` preset and packs
`out\release\FractureRecomp-v0.3.0-windows-x64.zip`: `fracture.exe`, the
SDK's DLLs (checked against the SDK's own files), the Visual C++ runtime DLLs
they import, `README.txt` and the third-party licenses. It checks every
shipped binary's imports (each must be shipped or part of Windows) and packs
only that list: never `game\`, the `.iso`, generated code, PDBs or the
maintainer's `fracture.toml` / `settings.toml`. Codegen needs the game files,
so this runs on the maintainer's machine, not in CI.

## Settings

In the game, press **Esc** or **F1**, or **RB + Start** on a controller
(R1 + Options on a DualSense), to open the settings screen. It works with the
keyboard, the mouse and a controller; the game gets no input while it is open.

| Category | Settings |
| --- | --- |
| Display | Fullscreen, monitor, window size, keep aspect ratio, allow tearing |
| Graphics | Frame rate (30, or 60 as an experiment), graphics API (Direct3D 12 or Vulkan), graphics card, internal resolution (1x to 4x), anti-aliasing (FXAA), texture filtering, render path (fast or accurate) |
| Controls | Controller API (SDL or XInput), the shortcut that opens the screen, keyboard and mouse, mouse look and sensitivity |
| Audio | Mute, audio latency |
| System | Restart now, revert pending changes, close the screen, quit |

| Keyboard | Controller | Action |
| --- | --- | --- |
| Arrows | D-pad, left stick | Select, change the value |
| Enter, Space | A | Confirm |
| Esc, Backspace | B | Back (rows, categories, closed) |
| Q / E | LB / RB | Previous / next category |
| R twice | X twice | Restart now (when changes are pending) |

Some settings apply at once. The others turn yellow and take effect after a
restart: press R (X on a controller) twice, or use System > Restart Now.

- Choices are saved to `settings.toml` next to `fracture.exe`. It is applied
  after `fracture.toml`, so its values win; delete a line to go back to the
  default.
- Command-line flags such as `--resolution_scale=2` win over both files. The
  screen marks those settings with `*`.
- Graphics API "Automatic" uses Vulkan under Proton or Wine (Steam Deck,
  Linux) and Direct3D 12 on Windows. Without a Vulkan driver or GPU the game
  starts with Direct3D 12. If Vulkan is chosen and the game then fails to
  start, delete the `fracture_graphics_api` line from `settings.toml`.
- The game was made for 30 FPS. At 60 it kept its normal speed in testing,
  but some of its timers count frames, so 60 stays marked experimental.
- The SDK's own overlays are still there: F3 toggles its debug overlay, the
  backtick key opens a log console, and F4 lists every cvar for advanced use.
  F4's "Save to config" writes `fracture.toml`.

## Development

| Path | Purpose |
| --- | --- |
| `fracture_manifest.toml` | Codegen manifest: input XEX and config includes |
| `config/fracture_functions.toml` | Hand-checked function boundary overrides |
| `config/fracture_data_refs.toml` | Function starts referenced only from data, found by scanning the guest image |
| `config/fracture_code_refs.toml` | Function starts whose address is built in code with `lis` + `addi`/`ori` |
| `src/fracture_app.h` | App hooks: Xenos GPU by default, finding the game files (or the install screen), settings screen, image dump |
| `src/fracture_hooks.cpp` | Hand-written guest functions that codegen cannot express |
| `src/fracture_settings.{h,cpp}` | `settings.toml`, restart, `--fracture_dump_cvars` (lists every cvar) |
| `src/fracture_settings_dialog.{h,cpp}` | The settings screen |
| `src/fracture_menu_input.{h,cpp}` | Controller input for the screen: wraps the game's `XamInputGetState` |
| `src/fracture_frame_rate.{h,cpp}` | The 60 FPS option: the game's Direct3D presentation interval |
| `src/fracture_graphics.{h,cpp}` | Direct3D 12 or Vulkan: loads the GPU plugin with the chosen API, lists the GPUs |
| `src/fracture_installer.{h,cpp}` | First start: reads the disc image, checks `default.xex`, extracts the game files |
| `src/fracture_installer_dialog.{h,cpp}` | The install screen shown before the game starts |
| `src/fracture_ui.h`, `src/fracture_paths.h` | Shared drawing helpers; paths as UTF-8 |
| `src/fracture.manifest` | Windows manifest: UTF-8 as the process code page |
| `tools/setup.py` | The setup program |
| `tools/package_release.py` | Packs a player release (see [Making a release](#making-a-release)) |
| `tools/xdvdfs.py` | Xbox disc image reader |
| `tools/dev/` | Analysis helpers used to produce the configs (`scan_refs.py`, `prune_splits.py`, `fallthrough.py`) |

Findings so far:

- **vtable-only methods.** Many small virtual methods are reached only through
  vtables, static-initializer tables or this-adjusting thunks, so the analyzer
  never marks them as code. `config/fracture_data_refs.toml` lists 2199 of them,
  found by dumping the loaded image (`fracture.exe --fracture_dump_image=image.bin`)
  and running `tools/dev/scan_refs.py`. Hits that turned out to be `switch`
  labels inside other functions were removed after codegen reported
  cross-function branches for them.
- **Rethrow fall-through.** `0x8224AC00` is `_CxxThrowException`. Functions
  that end in a rethrow (`r3 = r4 = 0`) are followed by exception-handler data,
  and the analyzer ran on into it; `tools/dev/fallthrough.py` finds them and
  the overrides end each one right after the call.
- **Addresses built in code.** Callbacks and pointers to virtual member
  functions (for example the virtual-call thunk at `0x823A01E0`) are formed
  with `lis` and a later `addi`/`ori`, often with the high half kept in a
  non-volatile register far from its use. `scan_refs.py --code` tracks `lis`
  values through each function; `config/fracture_code_refs.toml` lists the
  120 hits that survived.
- **Reconciling scans with codegen.** `tools/dev/prune_splits.py` reads a
  codegen log and fixes the scanned list: it drops hits that cut another
  function in two (catch-handler continuation labels in `0x8272xxxx`, switch
  labels) and adds functions that a scanned thunk tail-calls. Run codegen and
  the script until codegen is clean.
- **Shared tails.** The compiler merges identical function endings, so a
  function can run into, or branch into, the tail of its neighbour. Guest
  functions cannot overlap, so `0x82DFDD68` gets its shared tail from a
  hand-written copy in `src/fracture_hooks.cpp`, and `0x8263A850` is
  declared whole in `config/fracture_functions.toml`.
- **Frame rate.** The game's Direct3D presents every second vblank
  (`D3DPRESENT_INTERVAL_TWO`). The interval is read from the device in
  `sub_822A4350`; a hook there swaps in a shorter one and calls the original
  (`__imp__sub_822A4350`), so the guest vblank stays at 60 Hz.
- **Controller input.** Generated code calls the SDK's `XamInputGetState`
  directly, so `src/fracture_menu_input.cpp` defines that symbol, calls the
  SDK's function and filters what the game reads.
- **Vulkan.** The SDK's Windows package is built with Direct3D 12 only
  (`REXGLUE_USE_VULKAN` defaults to off on Windows), so setup builds the SDK
  from source with it on. Git checks the SDK's symlinks out as text files on
  Windows; setup replaces the ones the build compiles (libmspack) with copies
  of their targets.
- **First start.** `ReXApp::OnFinalizePaths` may return no paths and resume
  later, after the window and the ImGui drawer exist but before the runtime
  does, so the install screen runs there and starts the game when done.
  Files are written as `.part` and renamed, and `default.xex` comes last, so
  an interrupted copy never looks like an installed game.
- **Non-ASCII paths.** The SDK turns paths into text with `path::string()`
  and back from `char*`, which on Windows means the ANSI code page, while
  toml++, SDL and the logs use UTF-8. In a folder such as
  `C:\Users\Иван\Downloads`, `settings.toml` did not load, a dropped file
  arrived garbled, and the achievements in a localized `OneDrive\Документы`
  could not be read. `src/fracture.manifest` makes UTF-8 the process's code
  page (Windows 10 1903 and later), which fixes all of them; the settings
  code also hands the SDK the 8.3 short name of a config file, for systems
  that ignore the manifest.
- **Next:** missing kernel imports and GPU issues as they come up; a native
  Linux build for the Steam Deck.

The image dump is decrypted retail code: keep it out of the repository.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), the recompilation SDK
  used by this project
- [Xenia](https://github.com/xenia-project/xenia), for Xbox 360 research
- [skate3recomp](https://github.com/mchughalex/skate3recomp), whose layout this
  project follows

Fracture is a trademark of its respective owners. This project is not
affiliated with or endorsed by them or by Microsoft.
