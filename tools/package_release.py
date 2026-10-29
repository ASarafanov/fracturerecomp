"""Packs a player release: fracture.exe with everything it needs, as a zip.

usage: python tools/package_release.py --version 0.3.0 [--no-build]

Run it on the maintainer's machine after tools/setup.py has set things up
for both supported releases (codegen needs xex/v1/default.xex and
xex/v2/default.xex, so CI cannot do this). It builds the win-amd64-release
preset, then copies an explicit list of files into
out/release/FractureRecomp/ and zips that folder:

- fracture.exe, the game code of each release (fracture_v1.dll,
  fracture_v2.dll; a package always carries both) and the SDK DLLs next to
  them (checked byte for byte against the SDK's own bin folder);
- the VC++ runtime DLLs they import, from the Visual Studio redist folder
  (app-local, so players need no redistributable installer);
- README.txt for players and licenses/ (SDK and third-party notices).

Every shipped binary's imports are checked: each DLL must be shipped or be
part of Windows. Game files (game/, default.xex, the .iso), generated code,
image dumps, PDBs and config files (fracture.toml holds the maintainer's
paths, settings.toml their choices) are never packed.
"""
import argparse
import hashlib
import shutil
import struct
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import setup  # noqa: E402

ROOT = setup.ROOT
PRESET = "win-amd64-release"
BUILD_DIR = ROOT / "out" / "build" / PRESET
SDK_BIN = setup.SDK_PREFIX / "bin"
# Built by this project, one per release (src/game/game_api.h).
GAME_DLLS = ("fracture_v1.dll", "fracture_v2.dll")
SDK_THIRDPARTY = setup.SDK_SRC / "thirdparty"
OUT_DIR = ROOT / "out" / "release"
PACKAGE_NAME = "FractureRecomp"
REPO_URL = "https://github.com/ASarafanov/fracturerecomp"

# Microsoft's app-local VC++ runtime (Microsoft.VC143.CRT, "Distributable
# Code" of Visual Studio).
VC_RUNTIME = {
    "concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll", "vccorlib140.dll",
    "vcruntime140.dll", "vcruntime140_1.dll",
}
# Part of Windows 10/11. Anything else a shipped binary imports has to be
# shipped, or added here once checked.
SYSTEM_DLLS = {
    "advapi32.dll", "avrt.dll", "bcrypt.dll", "cfgmgr32.dll", "comctl32.dll",
    "comdlg32.dll", "crypt32.dll", "d3d11.dll", "d3d12.dll", "dbghelp.dll",
    "dinput8.dll", "dwmapi.dll", "dxgi.dll", "gdi32.dll", "hid.dll",
    "imm32.dll", "iphlpapi.dll", "kernel32.dll", "ntdll.dll", "ole32.dll",
    "oleaut32.dll", "powrprof.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shcore.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "userenv.dll", "uxtheme.dll", "version.dll", "winmm.dll", "ws2_32.dll",
    "xinput1_4.dll",
}
SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-")

# Third-party notices from the SDK source: component -> license files,
# relative to its thirdparty folder. Test-only and macOS-only components
# (catch2, moltenvk) are not shipped.
LICENSES = {
    "FFmpeg": ["FFmpeg/LICENSE.md", "FFmpeg/COPYING.LGPLv2.1"],
    "aes_128": ["aes_128/LICENSE"],
    "cli11": ["cli11/LICENSE"],
    "crypto-des": ["crypto/des/LICENSE"],
    "fmt": ["fmt/LICENSE"],
    "glslang": ["glslang/LICENSE.txt"],
    "imgui": ["imgui/LICENSE.txt"],
    "libmspack": ["libmspack/libmspack/COPYING.LIB"],
    "o1heap": ["o1heap/LICENSE"],
    "sdl3": ["sdl3/LICENSE.txt"],
    "simde": ["simde/COPYING"],
    "spdlog": ["spdlog/LICENSE"],
    "spirv-headers": ["spirv-headers/LICENSE"],
    "spirv-tools": ["spirv-tools/LICENSE"],
    "tomlplusplus": ["tomlplusplus/LICENSE"],
    "tracy": ["tracy/LICENSE"],
    "utfcpp": ["utfcpp/LICENSE"],
    "vulkan-headers": ["vulkan-headers/LICENSE.md"],
    "vulkan-loader": ["vulkan-loader/LICENSE.txt"],
    "vulkan-memory-allocator": ["vulkan-memory-allocator/LICENSE.txt"],
    "xxHash": ["xxHash/LICENSE"],
}

# Whatever happens above, these never go into a release.
FORBIDDEN_SUFFIXES = (".iso", ".xex", ".pdb", ".toml", ".bin", ".log", ".cpp", ".h")
MAX_FILE_SIZE = 256 << 20


class PackageError(Exception):
    pass


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pe_imports(path):
    """DLL names a PE file imports, normal and delay-loaded."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise PackageError(f"{path.name} is not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise PackageError(f"{path.name} is not a PE file")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    sections = []
    for i in range(section_count):
        _, virtual_size, address, raw_size, raw_offset = struct.unpack_from(
            "<8sIIII", data, optional + optional_size + i * 40)
        sections.append((address, max(virtual_size, raw_size), raw_offset))

    def offset(rva):
        for address, size, raw_offset in sections:
            if address <= rva < address + size:
                return rva - address + raw_offset
        raise PackageError(f"{path.name}: bad import table")

    def name_at(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii").lower()

    names = []
    # Import directory (1): 20-byte descriptors, name RVA at +12.
    # Delay-load directory (13): 32-byte descriptors, name RVA at +4.
    for index, size, name_field in ((1, 20, 12), (13, 32, 4)):
        rva = struct.unpack_from("<I", data, directories + index * 8)[0]
        if not rva:
            continue
        entry = offset(rva)
        while True:
            name_rva = struct.unpack_from("<I", data, entry + name_field)[0]
            if not name_rva:
                break
            names.append(name_at(name_rva))
            entry += size
    return names


def vc_runtime_dir(env):
    """The VC++ runtime of the toolset that built the binaries."""
    redist = env.get("VCToolsRedistDir")
    crt = Path(redist, "x64", "Microsoft.VC143.CRT") if redist else None
    if not crt or not (crt / "vcruntime140.dll").is_file():
        raise PackageError(f"VC++ runtime not found (VCToolsRedistDir={redist})")
    return crt


def collect_binaries(env):
    """Shipped binaries: name -> source path."""
    exe = BUILD_DIR / "fracture.exe"
    if not exe.is_file():
        raise PackageError(f"{exe} is missing; build first (or drop --no-build)")
    files = {"fracture.exe": exe}
    # Loaded at runtime by fracture.exe, so not in its import table either.
    for name in GAME_DLLS:
        dll = BUILD_DIR / name
        if not dll.is_file():
            raise PackageError(
                f"{dll} is missing; a release needs the game code of both releases "
                "(xex/v1/default.xex and xex/v2/default.xex)")
        files[name] = dll
    # The build copies the SDK DLLs next to the exe (the GPU plugin is loaded
    # at runtime, so it is not in any import table). Only ship them when they
    # are exactly the SDK's own files.
    for dll in sorted(BUILD_DIR.glob("*.dll")):
        if dll.name in GAME_DLLS:
            continue
        sdk_dll = SDK_BIN / dll.name
        if not sdk_dll.is_file() or sha256(sdk_dll) != sha256(dll):
            raise PackageError(f"{dll.name} in the build folder is not the SDK's file")
        files[dll.name] = dll
    if "rexruntime.dll" not in files or "rexgpu-xenos.dll" not in files:
        raise PackageError("rexruntime.dll / rexgpu-xenos.dll missing in the build folder")

    vc_dir = None
    todo = list(files)
    while todo:
        name = todo.pop()
        for imported in pe_imports(files[name]):
            if imported in files or imported.startswith(SYSTEM_PREFIXES) \
                    or imported in SYSTEM_DLLS:
                continue
            if imported in VC_RUNTIME:
                vc_dir = vc_dir or vc_runtime_dir(env)
                source = vc_dir / imported
                if not source.is_file():
                    raise PackageError(f"{imported} is not in {vc_dir}")
                files[imported] = source
                todo.append(imported)
                continue
            raise PackageError(
                f"{name} imports {imported}, which is neither shipped nor a known "
                "Windows DLL; ship it or add it to SYSTEM_DLLS after checking")
    return files


def readme(version):
    return f"""Fracture Recomp {version}
========================

An unofficial native Windows port of the Xbox 360 game Fracture (2008),
made by static recompilation with the ReXGlue SDK. Source and help:
{REPO_URL}

This archive contains the program, not the game. You need your own Xbox 360
disc image (.iso) of Fracture (title ID 4C4107D4). Two releases work: the
one with default.xex version 1 and the multi-language USA / Europe disc
(version 2). The program checks the disc and uses the matching game code,
fracture_v1.dll or fracture_v2.dll.

Requirements
------------
- Windows 10 (version 1903 or later) or 11, 64-bit, or a Steam Deck / Linux
  with Proton
- A graphics card with Direct3D 12 or Vulkan
- About 4 GB of free space for the game files

First start
-----------
1. Unpack this folder somewhere you can write to, for example
   Documents\\Games\\FractureRecomp. Not into Program Files: settings and
   logs are written next to fracture.exe.
2. Start fracture.exe. Windows may show "Windows protected your PC" because
   the program is not signed: click "More info", then "Run anyway".
3. Choose your disc image (Browse..., or drop the .iso onto the window) and
   where to install the game files (default: the game folder next to
   fracture.exe), then Install. The disc image is only read.
4. Press Play. Later starts go straight to the game. The .iso is no longer
   needed.

To update, unpack the new version over this folder; the game folder and
your settings stay.

In the game
-----------
Esc or F1, or RB + Start on a controller, opens the settings screen
(graphics API, resolution, frame rate, controls, audio).

Files
-----
- game\\: the game files from your disc (if installed there)
- settings.toml: your settings
- logs\\: log files, useful for bug reports
- Saves go to Documents\\fracture.

Steam Deck
----------
Copy the folder to the Deck, add fracture.exe as a non-Steam game and turn
on Proton for it (Properties > Compatibility). Not tested yet.

Legal
-----
Fracture is a trademark of its respective owners. This project is not
affiliated with or endorsed by them or by Microsoft. Use it only with a
copy of the game you own. Third-party licenses are in the licenses folder.
"""


def third_party_notes():
    lines = [
        "Third-party software in this program",
        "====================================",
        "",
        "fracture.exe, fracture_v1.dll, fracture_v2.dll, rexruntime.dll and",
        "rexgpu-xenos.dll are built with the",
        "ReXGlue SDK 0.10.0 (BSD 3-Clause, licenses/rexglue-sdk), which is",
        "built with the components below (not every one ends up in these",
        "files). Each folder here holds the license files from the",
        "component's source in the SDK.",
        "",
        "FFmpeg (libavcodec, libavutil; WMA Pro / XMA audio) and libmspack are",
        "under the GNU LGPL 2.1 and are linked into rexruntime.dll. Their",
        "source, and the build that makes rexruntime.dll, is the ReXGlue SDK",
        "at https://github.com/rexglue/rexglue-sdk (tag v0.10.0, with",
        "submodules); a rebuilt rexruntime.dll can replace the shipped one.",
        "FFmpeg is built without its GPL parts.",
        "",
        "The Microsoft Visual C++ runtime DLLs (msvcp140*.dll,",
        "vcruntime140*.dll) are redistributed under the Visual Studio license",
        "terms for Distributable Code.",
        "",
        "Components:",
    ]
    lines += [f"- {name}" for name in sorted(LICENSES)]
    return "\n".join(lines) + "\n"


def copy_licenses(dest):
    if not SDK_THIRDPARTY.is_dir():
        raise PackageError(
            f"{SDK_THIRDPARTY} is missing: the SDK source is needed for its "
            "license files (run tools/setup.py)")
    (dest / "rexglue-sdk").mkdir(parents=True)
    shutil.copy2(setup.SDK_SRC / "LICENSE", dest / "rexglue-sdk" / "LICENSE")
    for component, names in LICENSES.items():
        (dest / component).mkdir()
        for name in names:
            source = SDK_THIRDPARTY / name
            if not source.is_file():
                raise PackageError(f"license file missing: {source}")
            shutil.copy2(source, dest / component / source.name)
    (dest / "THIRD-PARTY.txt").write_text(third_party_notes(), newline="\r\n")


def check_package(folder):
    """Belt and braces: nothing from the game, the build or the configs."""
    for path in folder.rglob("*"):
        if path.is_dir():
            if path.name.lower() in ("game", "generated", "logs"):
                raise PackageError(f"unexpected folder in the package: {path}")
            continue
        name = path.name.lower()
        in_licenses = "licenses" in path.relative_to(folder).parts
        if name.endswith(FORBIDDEN_SUFFIXES) or name == "default.xex":
            raise PackageError(f"refusing to pack {path}")
        if not in_licenses and not name.endswith((".exe", ".dll", ".txt")):
            raise PackageError(f"unexpected file in the package: {path}")
        if path.stat().st_size > MAX_FILE_SIZE:
            raise PackageError(f"{path} is unexpectedly large")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--version", required=True, help="release version, e.g. 0.3.0")
    parser.add_argument("--no-build", action="store_true",
                        help=f"package the existing {PRESET} build as it is")
    args = parser.parse_args()
    version = args.version.lstrip("v")

    env = setup.find_toolchain()
    if not args.no_build:
        setup.build_sdk(env, "release")
        setup.build(env, "release")

    setup.step("Collecting files")
    binaries = collect_binaries(env)
    package = OUT_DIR / PACKAGE_NAME
    if package.exists():
        setup.remove_tree(package)
    package.mkdir(parents=True)
    for name, source in sorted(binaries.items()):
        shutil.copy2(source, package / name)
        print(f"{source.stat().st_size:>12}  {name}")
    (package / "README.txt").write_text(readme(version), newline="\r\n")
    copy_licenses(package / "licenses")
    check_package(package)

    setup.step("Zipping")
    archive = OUT_DIR / f"{PACKAGE_NAME}-v{version}-windows-x64.zip"
    archive.unlink(missing_ok=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                z.write(path, Path(PACKAGE_NAME) / path.relative_to(package))
    print(f"{archive}")
    print(f"{archive.stat().st_size / 1e6:.1f} MB, SHA-256 {sha256(archive)}")


if __name__ == "__main__":
    try:
        main()
    except (PackageError, setup.SetupError) as e:
        print(f"\nerror: {e}", file=sys.stderr)
        sys.exit(1)
