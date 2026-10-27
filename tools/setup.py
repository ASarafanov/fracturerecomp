"""Prepare Fracture for launch from your own Xbox 360 disc image.

1. Check the disc image is the supported Fracture release.
2. Extract the game files into game/.
3. Download the ReXGlue SDK (checksum-verified) into third_party/.
4. Recompile default.xex and build fracture.exe.
5. Write fracture.toml next to fracture.exe so it finds the game files.

usage: setup.bat [path\\to\\Fracture.iso] [--config relwithdebinfo|release|debug]
                 [--force]
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

import xdvdfs

ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = ROOT / "game"

# The function overrides in config/ were made against this exact executable.
TITLE_ID = 0x4C4107D4
DEFAULT_XEX_SHA256 = (
    "13fa6817089233d6d2b30adeca5c8bf74871ea28d0c620253aade6ee89557db2")
SKIPPED_ISO_DIRS = ("$SystemUpdate/",)  # console dashboard update, not game data

SDK_VERSION = "0.10.0"
SDK_ZIP = f"rexglue-sdk-{SDK_VERSION}-win-amd64.zip"
SDK_URL = ("https://github.com/rexglue/rexglue-sdk/releases/download/"
           f"v{SDK_VERSION}/{SDK_ZIP}")
SDK_SHA256 = "ad4485da853fbe932e9f5dbb643f9b5fe85dc73775ebbad4df6879fe6df0c11b"
SDK_DIR = ROOT / "third_party" / "rexglue-sdk" / SDK_VERSION
SDK_REXGLUE = SDK_DIR / "win-amd64" / "bin" / "rexglue.exe"

MIN_CLANG = 20


class SetupError(Exception):
    pass


def step(text):
    print(f"\n== {text}", flush=True)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def xex_title_id(data):
    """Title ID from the XEX2 execution info header, or None."""
    if data[:4] != b"XEX2":
        return None
    count = struct.unpack_from(">I", data, 20)[0]
    for i in range(count):
        key, value = struct.unpack_from(">II", data, 24 + i * 8)
        if key == 0x00040006:
            return struct.unpack_from(">I", data, value + 12)[0]
    return None


# --- 1. disc image --------------------------------------------------------

def find_iso(arg):
    if arg:
        path = Path(arg)
        if not path.is_file():
            raise SetupError(f"disc image not found: {path}")
        return path
    isos = sorted(ROOT.glob("*.iso"))
    if len(isos) == 1:
        return isos[0]
    try:
        import tkinter
        from tkinter import filedialog
        tkinter.Tk().withdraw()
        picked = filedialog.askopenfilename(
            title="Select your Fracture (Xbox 360) disc image",
            filetypes=[("Disc images", "*.iso"), ("All files", "*.*")])
    except Exception:
        picked = input("Path to your Fracture (Xbox 360) .iso: ").strip('" ')
    if not picked:
        raise SetupError("no disc image selected")
    return Path(picked)


def check_iso(iso, force):
    step(f"Checking disc image {iso}")
    with open(iso, "rb") as f:
        try:
            entries = xdvdfs.list_entries(f)
        except ValueError as e:
            raise SetupError(f"{iso}: {e}")
        by_path = {e.path.lower(): e for e in entries}
        xex = by_path.get("default.xex")
        if xex is None:
            raise SetupError("default.xex is missing from the disc image")
        data = xdvdfs.read_file(f, xex)
    title_id = xex_title_id(data)
    if title_id != TITLE_ID:
        raise SetupError(
            f"this is not Fracture (title ID {title_id or 0:08X}, "
            f"expected {TITLE_ID:08X})")
    digest = hashlib.sha256(data).hexdigest()
    if digest != DEFAULT_XEX_SHA256:
        message = ("default.xex differs from the supported release; the "
                   "function overrides in config/ will not match it")
        if not force:
            raise SetupError(message + " (pass --force to try anyway)")
        print("warning: " + message)
    print(f"Fracture, title ID {TITLE_ID:08X}, default.xex verified")
    return [e for e in entries if not e.is_dir
            and not e.path.startswith(SKIPPED_ISO_DIRS)]


def extract_game(iso, entries):
    step(f"Extracting game files into {GAME_DIR}")
    todo = [e for e in entries
            if not (GAME_DIR / e.path).is_file()
            or (GAME_DIR / e.path).stat().st_size != e.size]
    if not todo:
        print(f"all {len(entries)} files already extracted")
        return
    total = sum(e.size for e in todo)
    done = 0
    last_pct = -1
    with open(iso, "rb") as f:
        for e in todo:
            def progress(n):
                nonlocal done, last_pct
                done += n
                pct = done * 100 // total
                if pct != last_pct:
                    last_pct = pct
                    print(f"\r{pct:3d}%  {done / 1e9:.2f} / {total / 1e9:.2f} GB",
                          end="", flush=True)
            xdvdfs.extract(f, e, str(GAME_DIR / e.path), progress)
    print(f"\nextracted {len(todo)} files")


# --- 2. SDK ---------------------------------------------------------------

def fetch_sdk():
    step(f"ReXGlue SDK {SDK_VERSION}")
    if SDK_REXGLUE.is_file():
        print(f"already installed in {SDK_DIR}")
        return
    archive_dir = SDK_DIR.parent / "_archive"
    archive_dir.mkdir(parents=True, exist_ok=True)
    archive = archive_dir / SDK_ZIP
    if not archive.is_file() or sha256_file(archive) != SDK_SHA256:
        print(f"downloading {SDK_URL}")
        with urllib.request.urlopen(SDK_URL) as r, open(archive, "wb") as out:
            shutil.copyfileobj(r, out)
    if not archive.is_file():
        raise SetupError(
            f"{archive} disappeared right after download. Antivirus software "
            "(e.g. Microsoft Defender) may have removed it; see README.md.")
    digest = sha256_file(archive)
    if digest != SDK_SHA256:
        archive.unlink()
        raise SetupError(f"SDK archive checksum mismatch: {digest}")
    print("checksum OK, unpacking")
    with zipfile.ZipFile(archive) as z:
        z.extractall(SDK_DIR)
    if not SDK_REXGLUE.is_file():
        raise SetupError(
            f"{SDK_REXGLUE} is missing after unpacking. Antivirus software may "
            "have removed it; see README.md.")


# --- 3. toolchain ---------------------------------------------------------

INSTALL_HINT = """
Install the build tools (once), then run setup again:
  winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  winget install --id LLVM.LLVM"""


def vs_environment():
    """Environment of a VS 2022 x64 developer prompt, as a dict."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                   "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.is_file():
        raise SetupError("Visual Studio Build Tools not found." + INSTALL_HINT)
    install = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True, text=True).stdout.strip()
    vcvars = Path(install, "VC", "Auxiliary", "Build", "vcvars64.bat")
    if not install or not vcvars.is_file():
        raise SetupError("Visual Studio C++ build tools not found." + INSTALL_HINT)
    with tempfile.TemporaryDirectory() as tmp:
        script = Path(tmp, "env.bat")
        script.write_text(f'@call "{vcvars}" >nul\n@set\n')
        out = subprocess.run(["cmd", "/c", str(script)], capture_output=True,
                             text=True, errors="replace").stdout
    env = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    if "INCLUDE" not in env:
        raise SetupError(f"failed to set up the build environment with {vcvars}")
    return env


def find_toolchain():
    step("Checking build tools")
    env = vs_environment()
    llvm_bin = Path(os.environ.get("ProgramFiles", r"C:\Program Files"),
                    "LLVM", "bin")
    path = env.get("Path") or env.get("PATH", "")
    if llvm_bin.is_dir():
        path = f"{llvm_bin};{path}"
    env.pop("PATH", None)
    env["Path"] = path
    missing = []
    for tool in ("clang", "clang++", "cmake", "ninja"):
        found = shutil.which(tool, path=path)
        print(f"{tool:8} {found or 'NOT FOUND'}")
        if not found:
            missing.append(tool)
    if missing:
        raise SetupError(f"missing: {', '.join(missing)}" + INSTALL_HINT)
    version = subprocess.run([shutil.which("clang", path=path), "--version"],
                             capture_output=True, text=True).stdout
    major = int(re.search(r"version (\d+)", version).group(1))
    if major < MIN_CLANG:
        raise SetupError(f"clang {major} is too old, need {MIN_CLANG} or newer"
                         + INSTALL_HINT)
    return env


# --- 4. build -------------------------------------------------------------

def build(env, config):
    preset = f"win-amd64-{config}"
    step(f"Recompiling default.xex and building ({preset}); "
         "this takes several minutes")
    # CMake only picks up the generated sources (generated/default/
    # sources.cmake) when it configures after codegen, so run codegen on its
    # own first, then configure again and build everything.
    for args in (["cmake", "--preset", preset],
                 ["cmake", "--build", "--preset", preset,
                  "--target", "fracture_codegen"],
                 ["cmake", "--preset", preset],
                 ["cmake", "--build", "--preset", preset]):
        cmake = shutil.which(args[0], path=env["Path"])
        if subprocess.run([cmake] + args[1:], cwd=ROOT, env=env).returncode:
            raise SetupError(f"'{' '.join(args)}' failed")
    exe = ROOT / "out" / "build" / preset / "fracture.exe"
    if not exe.is_file():
        raise SetupError(f"build finished but {exe} is missing")
    return exe


def write_config(exe):
    config = exe.with_name("fracture.toml")
    line = f'game_data_root = "{GAME_DIR.as_posix()}"'
    lines = config.read_text().splitlines() if config.is_file() else [
        "# fracture.exe settings (cvars). Written by tools/setup.py."]
    lines = [l for l in lines if not l.strip().startswith("game_data_root")]
    lines.append(line)
    config.write_text("\n".join(lines) + "\n")
    print(f"wrote {config}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("iso", nargs="?", help="your Fracture Xbox 360 .iso")
    parser.add_argument("--config", default="relwithdebinfo",
                        choices=("relwithdebinfo", "release", "debug"))
    parser.add_argument("--force", action="store_true",
                        help="continue with an unsupported default.xex")
    args = parser.parse_args()
    if sys.platform != "win32":
        raise SetupError("only Windows x64 is supported for now")

    iso = find_iso(args.iso)
    entries = check_iso(iso, args.force)
    extract_game(iso, entries)
    fetch_sdk()
    env = find_toolchain()
    exe = build(env, args.config)
    write_config(exe)
    step("Done")
    print(f"Run: {exe}")


if __name__ == "__main__":
    try:
        main()
    except SetupError as e:
        print(f"\nerror: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
