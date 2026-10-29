"""Prepare Fracture for launch from your own Xbox 360 disc image.

1. Check the disc image is the supported Fracture release.
2. Extract the game files into game/.
3. Find the build tools.
4. Build the ReXGlue SDK from its source (pinned commit) with Direct3D 12
   and Vulkan into third_party/; the SDK's own Windows package has no Vulkan.
5. Recompile default.xex and build fracture.exe.
6. Write fracture.toml next to fracture.exe so it finds the game files.

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
import stat
import tempfile
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
SDK_REPO = "https://github.com/rexglue/rexglue-sdk.git"
SDK_TAG = f"v{SDK_VERSION}"
# The tag's commit; git verifies every object (and the pinned submodule
# commits) against it, so this plays the part of a checksum.
SDK_COMMIT = "f5337cdc947ff6d4c4196737e2c807a48f2a1fc2"
SDK_ROOT = ROOT / "third_party" / "rexglue-sdk"
SDK_SRC = SDK_ROOT / "src"
# CMakePresets.json points CMAKE_PREFIX_PATH here.
SDK_PREFIX = SDK_ROOT / SDK_VERSION / "win-amd64"
# Written after a successful build: commit, "vulkan", installed configs.
SDK_STAMP = SDK_PREFIX / "fracture-sdk-build.txt"
SDK_CONFIGS = {"relwithdebinfo": "RelWithDebInfo", "release": "Release",
               "debug": "Debug"}

MIN_CLANG = 20


class SetupError(Exception):
    pass


def step(text):
    print(f"\n== {text}", flush=True)


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


# --- 2. toolchain ---------------------------------------------------------

INSTALL_HINT = """
Install the build tools (once), then run setup again:
  winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  winget install --id LLVM.LLVM
  winget install --id Git.Git"""


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
    # A Git installed after this terminal was opened is not on its PATH yet.
    git_cmd = Path(os.environ.get("ProgramFiles", r"C:\Program Files"), "Git", "cmd")
    if git_cmd.is_dir() and not shutil.which("git", path=path):
        path = f"{path};{git_cmd}"
    env.pop("PATH", None)
    env["Path"] = path
    missing = []
    for tool in ("clang", "clang++", "cmake", "ninja", "git"):
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


# --- 3. SDK ---------------------------------------------------------------

def run(args, cwd, env):
    tool = shutil.which(args[0], path=env["Path"])
    if subprocess.run([tool] + args[1:], cwd=cwd, env=env).returncode:
        raise SetupError(f"'{' '.join(args)}' failed")


def output(args, cwd, env):
    tool = shutil.which(args[0], path=env["Path"])
    return subprocess.run([tool] + args[1:], cwd=cwd, env=env, capture_output=True,
                          text=True).stdout.strip()


def remove_tree(path):
    """shutil.rmtree that also removes read-only files (git objects)."""
    def retry(function, target, _):
        os.chmod(target, stat.S_IWRITE)
        function(target)
    if not path.exists():
        return
    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=retry)
    else:
        shutil.rmtree(path, onerror=retry)


def sdk_configs(config):
    # The codegen tool (rexglue.exe) is only installed with Release.
    return sorted({"Release", SDK_CONFIGS[config]})


def read_stamp():
    try:
        lines = SDK_STAMP.read_text().split("\n")
    except OSError:
        return None, set()
    if len(lines) < 3 or lines[1] != "vulkan":
        return None, set()
    return lines[0], set(lines[2].split())


def fetch_sdk_source(env):
    if (SDK_SRC / ".git").exists() and output(
            ["git", "rev-parse", "HEAD"], SDK_SRC, env) == SDK_COMMIT:
        print(f"source already in {SDK_SRC}")
        run(["git", "submodule", "update", "--init", "--recursive", "--depth", "1",
             "--jobs", "8"], SDK_SRC, env)
        return
    remove_tree(SDK_SRC)
    print("downloading the SDK source with its submodules (about 700 MB)")
    run(["git", "clone", "--quiet", "--branch", SDK_TAG, "--depth", "1",
         "--recurse-submodules", "--shallow-submodules", "--jobs", "8", SDK_REPO,
         str(SDK_SRC)], ROOT, env)
    head = output(["git", "rev-parse", "HEAD"], SDK_SRC, env)
    if head != SDK_COMMIT:
        raise SetupError(f"{SDK_TAG} of {SDK_REPO} is {head}, expected {SDK_COMMIT}")


def copy_symlink_targets(env):
    """Git for Windows checks symlinks out as small text files holding the
    target path unless symlinks are enabled; the SDK build compiles some of
    them (libmspack's cabextract/mspack/*.c), so put the target files there."""
    repos = [SDK_SRC] + [
        SDK_SRC / line for line in output(
            ["git", "submodule", "foreach", "--quiet", "--recursive",
             "echo $displaypath"], SDK_SRC, env).splitlines() if line]
    copied = 0
    for repo in repos:
        for line in output(["git", "ls-files", "-s"], repo, env).splitlines():
            meta, name = line.split("\t", 1)
            mode, blob = meta.split()[:2]
            link = repo / name
            if mode != "120000" or link.is_symlink():
                continue
            target = (link.parent / output(["git", "cat-file", "-p", blob], repo,
                                           env)).resolve()
            if target.is_file():
                shutil.copyfile(target, link)
                copied += 1
    if copied:
        print(f"replaced {copied} symlink placeholders with their target files")


def forget_sdk_registration():
    """The SDK's install step adds its prefix to the CMake user package
    registry (value name = MD5 of the lower-cased prefix). The presets point
    CMake at the SDK directly, so take that entry out again."""
    name = hashlib.md5(SDK_PREFIX.as_posix().lower().encode()).hexdigest()
    subprocess.run(["reg", "delete", r"HKCU\Software\Kitware\CMake\Packages\rexglue",
                    "/v", name, "/f"], capture_output=True)


def build_sdk(env, config):
    wanted = sdk_configs(config)
    step(f"ReXGlue SDK {SDK_VERSION} with Direct3D 12 and Vulkan "
         f"({', '.join(wanted)})")
    commit, built = read_stamp()
    if commit == SDK_COMMIT and set(wanted) <= built:
        print(f"already built in {SDK_PREFIX}")
        return
    if commit != SDK_COMMIT:
        # Older setups unpacked the SDK's Direct3D-12-only package here.
        for old in (SDK_PREFIX, SDK_ROOT / "_archive"):
            if old.exists():
                print(f"removing {old}")
                remove_tree(old)
        built = set()
    fetch_sdk_source(env)
    copy_symlink_targets(env)
    print("configuring and building the SDK; this takes several minutes")
    run(["cmake", "--preset", "win-amd64", "-DREXGLUE_USE_VULKAN=ON",
         f"-DCMAKE_INSTALL_PREFIX={SDK_PREFIX.as_posix()}"], SDK_SRC, env)
    for sdk_config in wanted:
        if sdk_config not in built:
            run(["cmake", "--build", "out/build/win-amd64", "--config", sdk_config,
                 "--target", "install", "--parallel", str(os.cpu_count() or 4)],
                SDK_SRC, env)
            built.add(sdk_config)
    forget_sdk_registration()
    if not (SDK_PREFIX / "bin" / "rexglue.exe").is_file():
        raise SetupError(f"SDK build finished but rexglue.exe is missing in {SDK_PREFIX}")
    SDK_STAMP.write_text(f"{SDK_COMMIT}\nvulkan\n{' '.join(sorted(built))}\n")
    # The intermediate files are most of the 2 GB; the source stays for the
    # next run.
    remove_tree(SDK_SRC / "out")


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
        run(args, ROOT, env)
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
    env = find_toolchain()
    build_sdk(env, args.config)
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
