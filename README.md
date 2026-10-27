# fracturerecomp

An unofficial native recompilation of the Xbox 360 version of **Fracture**
(Day 1 Studios / LucasArts, 2008) for Windows, built on the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

The game's PowerPC code is translated to C++ ahead of time and compiled into a
regular Windows executable. Rendering goes through the SDK's emulated Xenos GPU
(Direct3D 12 / Vulkan).

> **Status: early, not playable.** The project builds and the game starts
> executing: it gets through static initialization and its first file access,
> then stops about a second in with
> `Call to invalid or unregistered function at guest address 0x823A01E0`
> (a virtual-call thunk that the code takes the address of with `lis`/`addi`).
> See [Development](#development) for where this stands.

This repository contains **no game files and no code generated from them**. You
must provide your own legally obtained Xbox 360 copy of Fracture; everything
derived from it is produced on your machine by the setup program.

## Requirements

- Windows 10/11 x64
- Python 3.9 or newer
- Visual Studio 2022 Build Tools with the C++ workload (provides the MSVC
  libraries, the Windows SDK, CMake and Ninja)
- LLVM/Clang 20 or newer

Install the build tools with:

```
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id LLVM.LLVM
```

- A disc image (`.iso`) of the Xbox 360 release of Fracture. The supported
  executable is title ID `4C4107D4`, `default.xex` SHA-256
  `13fa6817089233d6d2b30adeca5c8bf74871ea28d0c620253aade6ee89557db2`.

## Setup

Drag your `.iso` onto `setup.bat`, or run:

```
setup.bat path\to\Fracture.iso
```

Without an argument it uses the single `.iso` in the repository folder, or asks
you to pick one. The setup program:

1. checks that the image is the supported Fracture release;
2. extracts the game files into `game\` (about 4 GB);
3. downloads the ReXGlue SDK 0.10.0 release and verifies its SHA-256 before
   unpacking it into `third_party\rexglue-sdk\`;
4. recompiles `default.xex` and builds `fracture.exe` (several minutes);
5. writes `fracture.toml` next to `fracture.exe` so it finds `game\`.

Running it again skips the steps that are already done. Pass
`--config release` or `--config debug` for another build type
(default: `relwithdebinfo`).

Then start `out\build\win-amd64-relwithdebinfo\fracture.exe`.

### Antivirus

Microsoft Defender has flagged the official ReXGlue SDK release archive as a
potentially unwanted program on at least one machine and removed it right
after download. The setup program checks the archive against the SHA-256
published on the SDK's GitHub release and stops if the file is altered or
missing. Whether to allow the file (Windows Security > Protection history) or
exclude the `third_party\rexglue-sdk` folder is your decision.

## Development

| Path | Purpose |
| --- | --- |
| `fracture_manifest.toml` | Codegen manifest: input XEX and config includes |
| `config/fracture_functions.toml` | Hand-checked function boundary overrides |
| `config/fracture_data_refs.toml` | Function starts referenced only from data, found by scanning the guest image |
| `src/fracture_app.h` | App hooks: Xenos GPU by default, `game_data_root` from `fracture.toml`, image dump |
| `tools/setup.py` | The setup program |
| `tools/xdvdfs.py` | Xbox disc image reader |
| `tools/dev/` | Analysis helpers used to produce the configs |

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
- **Next:** functions whose address is built in code with `lis`/`addi` (for
  example the virtual-call thunk at `0x823A01E0`), then missing kernel
  imports and GPU issues as they come up.

The image dump is decrypted retail code: keep it out of the repository.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), the recompilation SDK
  used by this project
- [Xenia](https://github.com/xenia-project/xenia), for Xbox 360 research
- [skate3recomp](https://github.com/mchughalex/skate3recomp), whose layout this
  project follows

Fracture is a trademark of its respective owners. This project is not
affiliated with or endorsed by them or by Microsoft.
