#!/usr/bin/env python3
"""SurfaceRT_OS build driver - one implementation for every host OS.

`make` on Linux/macOS is a thin wrapper around this file, and on Windows you run
the exact same code with

    py scripts\\build.py all

which is why there is only one place that knows the compiler flags, the object
list or the link order.

Commands:
    all        boot + kernel + services + image   (default)
    boot       BOOTARM.EFI (UEFI loader)
    kernel     KERNEL.ELF
    services   loadable .SVC modules
    image      assemble dist/ from the build output
    sim        host simulator for the display sink
    tests      host unit tests (C) + python tests
    clean      remove build/ and dist/
    doctor     print the detected toolchain and environment

Everything is written in plain Python 3 plus subprocess, so it behaves the same
in cmd.exe, PowerShell, bash and zsh.
"""

import argparse
import concurrent.futures
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""

# --------------------------------------------------------------------- flags --
COMMON_ARM = [
    "-mcpu=cortex-a9", "-marm", "-mfloat-abi=soft", "-mno-unaligned-access",
    "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-ffunction-sections", "-fdata-sections",
    "-Wall", "-Wextra", "-Wno-unused-parameter",
    "-Wno-address-of-packed-member",
]

BOOT_SOURCES = ["boot/loader.c", "boot/elfload.c", "kernel/lib/vsnprintf.c",
                "kernel/lib/string.c"]


def boot_flags(cross_dir):
    return COMMON_ARM + [
        "-fshort-wchar", "-fPIC", "-fvisibility=hidden", "-fno-plt",
        "-Os", "-g",
        "-I" + str(ROOT / "boot"),
        "-I" + str(ROOT / "kernel" / "include"),
    ]


sys.path.insert(0, str(ROOT / "scripts"))
from platforms import PLATFORMS, DEFAULT_PLATFORM, describe  # noqa: E402


def kernel_flags(platform=DEFAULT_PLATFORM):
    soc = PLATFORMS.get(platform)
    if not soc:
        raise BuildError("unknown platform %r (known: %s)" %
                         (platform, ", ".join(sorted(PLATFORMS))))
    flags = [f for f in COMMON_ARM if not f.startswith("-mcpu")]
    return ["-mcpu=" + soc["mcpu"]] + flags + [
        "-D" + soc["define"] + "=1",
        "-fno-common", "-O2", "-g",
        "-I" + str(ROOT / "kernel" / "include"),
        "-I" + str(ROOT / "kernel" / "platform" / "tegra"),
        "-I" + str(ROOT / "kernel" / "platform" / platform),
    ]


def service_flags():
    return COMMON_ARM + [
        "-fPIC", "-fvisibility=hidden", "-fno-plt", "-Os", "-g",
        "-I" + str(ROOT / "kernel" / "include"),
    ]


def host_flags(host_cc):
    flags = ["-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
             "-DSRTOS_HOST", "-I" + str(ROOT / "kernel" / "include")]
    if "cl" == Path(host_cc).name.lower().replace(".exe", ""):
        flags = ["/O1", "/DSRTOS_HOST", "/I", str(ROOT / "kernel" / "include"),
                 "/w"]
    return flags


# ------------------------------------------------------------------ helpers --
class BuildError(RuntimeError):
    pass


def run(cmd, cwd=None, capture=False, quiet=False):
    if not quiet and not capture:
        print("    " + " ".join(str(c) for c in cmd[:2]) +
              (" ..." if len(cmd) > 2 else ""))
    kwargs = dict(cwd=str(cwd) if cwd else None)
    if capture:
        kwargs.update(stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    proc = subprocess.run([str(c) for c in cmd], **kwargs)
    if proc.returncode != 0:
        if capture and proc.stdout:
            print(proc.stdout)
        raise BuildError("command failed (%d): %s" % (proc.returncode,
                                                      " ".join(map(str, cmd))))
    return proc.stdout if capture else ""


def find_tool(prefix, name):
    exe = prefix + name + EXE
    return exe


def normalize_cross(spec):
    r"""Accept a prefix, a bin directory or the gcc binary itself.

    People copy whatever their shell or Explorer shows them, so all of these
    end up as a usable prefix:

        --cross arm-none-eabi-
        --cross C:\msys64\mingw64\bin
        --cross C:\msys64\mingw64\bin\
        --cross C:\msys64\mingw64\bin\arm-none-eabi-gcc.exe
        --cross /usr/bin/arm-none-eabi-
    """
    if not spec:
        return spec
    unified = spec.replace("\\", "/").rstrip("/")
    base = unified.rsplit("/", 1)[-1].lower()
    if base in ("arm-none-eabi-gcc", "arm-none-eabi-gcc.exe"):
        parent = unified.rsplit("/", 1)[0] if "/" in unified else ""
        out = (parent + "/" if parent else "") + "arm-none-eabi-"
    elif base == "arm-none-eabi-":
        out = unified            # already a prefix; never append a separator
    elif "/" in unified:
        out = unified + "/arm-none-eabi-"
    else:
        out = spec
    return out.replace("/", os.sep)


TOOLCHAIN_MISSING_EXIT = 3


def resolve_cross(explicit):
    """Return the arm-none-eabi- prefix, or None."""
    candidates = []
    if explicit:
        candidates.append(normalize_cross(explicit))
    if os.environ.get("CROSS_COMPILE"):
        candidates.append(os.environ["CROSS_COMPILE"])
    for pattern in ("xpack-arm-none-eabi-gcc-*", "arm-none-eabi-gcc-*",
                    "gcc-arm-none-eabi-*"):
        for path in sorted(glob.glob(str(ROOT / ".toolchain" / pattern))):
            candidates.append(os.path.join(path, "bin", "arm-none-eabi-"))
    which = shutil.which("arm-none-eabi-gcc")
    if which:
        candidates.append("arm-none-eabi-")

    for cand in candidates:
        gcc = cand + "gcc" + EXE
        if os.path.sep not in cand and shutil.which(gcc):
            return cand
        if Path(gcc).exists():
            return cand
    return None


def resolve_host_cc(explicit=None):
    if explicit:
        return explicit
    for name in ("cc", "gcc", "clang", "cl"):
        found = shutil.which(name)
        if found:
            return found
    return None


def objects_dir(build, component):
    path = Path(build) / component
    path.mkdir(parents=True, exist_ok=True)
    return path


def object_path(build, component, source):
    src = Path(source)
    try:
        rel = src.resolve().relative_to(ROOT)
    except ValueError:
        rel = Path(src.name)
    target = Path(build) / component / rel.with_suffix(".o")
    target.parent.mkdir(parents=True, exist_ok=True)
    return target


def dep_headers(obj_path):
    """Parse the .d file emitted by -MMD -MP and return its prerequisites."""
    dep = obj_path.with_suffix(".d")
    if not dep.exists():
        return []
    text = dep.read_text(errors="replace").replace("\\\n", " ")
    _, _, rest = text.partition(":")
    headers = []
    for token in rest.split():
        token = token.rstrip("\\")
        if token and not token.endswith(".o"):
            headers.append(token)
    return headers


def stale(obj_path, source, extra_deps=()):
    if not obj_path.exists():
        return True
    obj_mtime = obj_path.stat().st_mtime
    if Path(source).stat().st_mtime > obj_mtime:
        return True
    for dep in list(extra_deps) + dep_headers(obj_path):
        if os.path.exists(dep) and os.stat(dep).st_mtime > obj_mtime:
            return True
    return False


def compile_many(jobs, tasks, verbose):
    """tasks: list of (cmd, label).  Runs them in parallel, stops on first error."""
    errors = []

    def work(task):
        cmd, label = task
        try:
            run(cmd, quiet=not verbose)
        except BuildError as exc:
            return "%s: %s" % (label, exc)
        return None

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for result in pool.map(work, tasks):
            if result:
                errors.append(result)
    if errors:
        raise BuildError("\n".join(errors))

# ------------------------------------------------------------------ targets --
def build_boot(cross, build, jobs, verbose):
    print("boot: UEFI loader")
    cc = cross + "gcc" + EXE
    ld_flags = ["-nostdlib", "-shared", "-Bsymbolic", "-Wl,--no-undefined",
                "-Wl,-e,efi_main", "-T", str(ROOT / "boot" / "loader.lds")]
    flags = boot_flags(cross)

    objs, tasks = [], []
    for src in BOOT_SOURCES:
        source = ROOT / src
        obj = object_path(build, "boot", source)
        objs.append(obj)
        if stale(obj, source):
            cmd = [cc] + flags + ["-MMD", "-MP", "-c", str(source), "-o", str(obj)]
            tasks.append((cmd, src))
    if tasks:
        compile_many(jobs, tasks, verbose)

    elf = Path(build) / "boot" / "boot.elf"
    if tasks or not elf.exists():
        run([cc] + flags + ld_flags + ["-o", str(elf)] + [str(o) for o in objs] +
            ["-lgcc"], quiet=not verbose)
        print("    " + run([cross + "size" + EXE, str(elf)], capture=True).strip()
              .replace("\n", "\n    "))

    efi = Path(build) / "boot" / "BOOTARM.EFI"
    if tasks or not efi.exists() or efi.stat().st_mtime < elf.stat().st_mtime:
        run([sys.executable, str(ROOT / "tools" / "elf2pe.py"), str(elf),
             str(efi)], quiet=not verbose)
    print("    -> %s (%d bytes)" % (efi.relative_to(ROOT), efi.stat().st_size))
    return 0


def kernel_sources(platform):
    patterns = ["kernel/arch/arm/*.c", "kernel/core/*.c", "kernel/lib/*.c",
                "kernel/drivers/*/*.c", "kernel/net/*.c", "kernel/fs/*.c",
                "kernel/services/*.c",
                "kernel/platform/tegra/*.c",
                "kernel/platform/%s/*.c" % platform]
    sources = []
    for pattern in patterns:
        sources += sorted(glob.glob(str(ROOT / pattern)))
    asm = sorted(glob.glob(str(ROOT / "kernel/arch/arm/*.S")))
    return sources, asm


def platform_stamp(build, platform, flags):
    """Force a full kernel rebuild when the board or the flags change."""
    stamp = Path(build) / "kernel" / ".platform"
    want = ("%s\n%s\n" % (platform, " ".join(flags))).replace("\\", "/")
    if stamp.exists():
        have = stamp.read_text(errors="replace").replace("\\", "/")
        if have == want:
            return False
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.write_text(want)
    return True


def build_kernel(cross, build, jobs, verbose, platform=DEFAULT_PLATFORM):
    soc = PLATFORMS.get(platform)
    if not soc:
        raise BuildError("unknown platform %r" % platform)
    print("kernel: %s  (%s)" % (platform, soc["board"]))
    cc = cross + "gcc" + EXE
    out = Path(build) / "kernel"
    out.mkdir(parents=True, exist_ok=True)
    flags = kernel_flags(platform)

    if platform_stamp(build, platform, flags):
        # different SoC or flags: the old objects are not reusable
        for stale_obj in list(out.glob("**/*.o")) + list(out.glob("**/*.d")):
            stale_obj.unlink(missing_ok=True)

    sources, asm = kernel_sources(platform)
    objs, tasks = [], []
    for src in sources:
        obj = object_path(build, "kernel", src)
        objs.append(obj)
        if stale(obj, src):
            cmd = [cc] + flags + ["-MMD", "-MP", "-c", src, "-o", str(obj)]
            tasks.append((cmd, os.path.relpath(src, ROOT)))
    for src in asm:
        obj = object_path(build, "kernel", src)
        objs.append(obj)
        if stale(obj, src):
            tasks.append(([cc] + flags + ["-c", src, "-o", str(obj)],
                          os.path.relpath(src, ROOT)))
    if tasks:
        compile_many(jobs, tasks, verbose)

    elf = out / "KERNEL.ELF"
    if tasks or not elf.exists():
        run([cc] + flags + [
            "-nostdlib", "-T", str(ROOT / "kernel" / "linker.lds"),
            "-Wl,--gc-sections", "-Wl,--emit-relocs", "-Wl,--build-id=none",
            "-Wl,-Map=" + str(out / "kernel.map"),
            "-o", str(elf)] + [str(o) for o in objs] + ["-lgcc"],
            quiet=not verbose)
        print("    " + run([cross + "size" + EXE, str(elf)], capture=True).strip()
              .replace("\n", "\n    "))
    print("    -> %s (%d bytes)" % (elf.relative_to(ROOT), elf.stat().st_size))
    return 0


def build_services(cross, build, jobs, verbose):
    print("services: loadable modules")
    cc = cross + "gcc" + EXE
    out = Path(build) / "services"
    out.mkdir(parents=True, exist_ok=True)
    flags = service_flags()
    ld_flags = ["-nostdlib", "-shared", "-Bsymbolic",
                "-Wl,-e,srt_module_init", "-Wl,--gc-sections",
                "-Wl,--build-id=none", "-Wl,-z,notext"]

    modules = sorted(p for p in glob.glob(str(ROOT / "services" / "*"))
                     if Path(p).is_dir() and (Path(p) / (Path(p).name + ".c")).exists())
    if not modules:
        print("    (no modules)")
        return 0
    for mod in modules:
        name = Path(mod).name
        src = Path(mod) / (name + ".c")
        target = out / (name + ".SVC")
        deps = [src, ROOT / "kernel" / "lib" / "string.c"]
        if target.exists() and not any(d.stat().st_mtime > target.stat().st_mtime
                                       for d in deps):
            continue
        run([cc] + flags + ld_flags + ["-o", str(target), str(src),
             str(ROOT / "kernel" / "lib" / "string.c"), "-lgcc"], quiet=not verbose)
        print("    -> %s (%d bytes)" % (target.relative_to(ROOT),
                                       target.stat().st_size))
    return 0


def build_sim(build, verbose, host_cc=None):
    print("sim: display sink simulator")
    cc = resolve_host_cc(host_cc)
    if not cc:
        raise BuildError("no host C compiler found (need cc/gcc/clang)")
    out = Path(build) / "sim"
    out.mkdir(parents=True, exist_ok=True)
    target = out / ("srtos-sim" + EXE)
    sources = [ROOT / "sim" / "sim_main.c",
               ROOT / "kernel" / "services" / "srdp_core.c",
               ROOT / "kernel" / "lib" / "string.c",
               ROOT / "kernel" / "lib" / "vsnprintf.c"]
    if target.exists() and not any(s.stat().st_mtime > target.stat().st_mtime
                                   for s in sources):
        print("    up to date")
        return 0
    run([cc] + host_flags(cc) + ["-o", str(target)] + [str(s) for s in sources],
        quiet=not verbose)
    print("    -> %s" % target.relative_to(ROOT))
    return 0


def build_tests(build, verbose, host_cc=None, run_tests=True):
    print("tests: host unit tests")
    cc = resolve_host_cc(host_cc)
    if not cc:
        raise BuildError("no host C compiler found (need cc/gcc/clang)")
    out = Path(build) / "tests"
    out.mkdir(parents=True, exist_ok=True)
    target = out / ("srtos-tests" + EXE)
    sources = [ROOT / "tests" / "test_main.c",
               ROOT / "kernel" / "lib" / "string.c",
               ROOT / "kernel" / "lib" / "vsnprintf.c",
               ROOT / "kernel" / "core" / "heap.c",
               ROOT / "kernel" / "services" / "srdp_core.c",
               ROOT / "boot" / "elfload.c"]
    run([cc] + host_flags(cc) + ["-o", str(target)] + [str(s) for s in sources],
        quiet=not verbose)
    if run_tests:
        kernel = Path(build) / "kernel" / "KERNEL.ELF"
        run([str(target), str(kernel)], quiet=False)
        run([sys.executable, str(ROOT / "tests" / "pytests" / "run_tests.py")],
            quiet=False)
    return 0


def build_image(build, out, esp_size=None, data_size=None, install_size=None,
                verbose=False, platform=DEFAULT_PLATFORM):
    def show(path):
        try:
            return str(Path(path).resolve().relative_to(ROOT))
        except ValueError:
            return str(path)

    print("image: assembling %s" % show(out))
    cmd = [sys.executable, str(ROOT / "scripts" / "mkimage.py"),
           "--build-dir", show(build), "--out", show(out),
           "--platform", platform]
    if esp_size:
        cmd += ["--esp-size", str(esp_size)]
    if data_size:
        cmd += ["--data-size", str(data_size)]
    if install_size:
        cmd += ["--install-size", str(install_size)]
    return run(cmd, quiet=not verbose) and 0 or 0


def clean(build, out):
    for path in (build, out):
        if Path(path).exists():
            shutil.rmtree(path, ignore_errors=True)
            print("removed %s" % path)
    return 0


def doctor(cross, build):
    print("SurfaceRT_OS build environment")
    print("  host os       : %s (%s)" % (sys.platform, os.name))
    print("  python        : %s" % sys.version.split()[0])
    print("  project root  : %s" % ROOT)
    print("  build dir     : %s" % (ROOT / build))
    if cross:
        try:
            version = run([cross + "gcc" + EXE, "--version"], capture=True)
            print("  arm toolchain : %s" % version.splitlines()[0])
            print("  CROSS_COMPILE : %s" % cross)
        except BuildError:
            print("  arm toolchain : %s (not runnable)" % cross)
    else:
        print("  arm toolchain : NOT FOUND")
        print("                  posix : ./toolchain/fetch-toolchain.sh")
        print("                  windows: powershell -ExecutionPolicy Bypass "
              "-File scripts\\fetch-toolchain.ps1")
    host_cc = resolve_host_cc()
    print("  host cc       : %s" % (host_cc or "not found (only sim/tests need it)"))
    print("  platforms     :")
    for name in sorted(PLATFORMS):
        soc = PLATFORMS[name]
        print("    %-9s %-12s %s" % (name, soc["mcpu"], soc["board"]))
    return 0 if cross else 1

# --------------------------------------------------------------------- main --
def main(argv=None):
    ap = argparse.ArgumentParser(
        description="SurfaceRT_OS build driver (works on Windows, Linux, macOS)",
        epilog="examples:  py scripts\\build.py all   |   python3 scripts/build.py test")
    ap.add_argument("command", nargs="?", default="all",
                    choices=["all", "boot", "kernel", "services", "image",
                             "sim", "tests", "test", "clean", "doctor",
                             "fetch-toolchain"])
    ap.add_argument("--build-dir", default="build")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--cross", default=None,
                    help="toolchain prefix, e.g. arm-none-eabi-")
    ap.add_argument("--host-cc", default=None)
    ap.add_argument("--platform", default=DEFAULT_PLATFORM,
                    choices=sorted(PLATFORMS),
                    help="target board: " + "; ".join(
                        "%s = %s" % (k, v["board"]) for k, v in
                        sorted(PLATFORMS.items())))
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--esp-size", type=int, default=0)
    ap.add_argument("--data-size", type=int, default=0)
    ap.add_argument("--install-size", type=int, default=0)
    ap.add_argument("--fetch", action="store_true",
                    help="download the pinned toolchain first if it is missing")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    build = Path(args.build_dir)
    out = Path(args.out)
    if not build.is_absolute():
        build = ROOT / build
    if not out.is_absolute():
        out = ROOT / out

    jobs = args.jobs or min(4, (os.cpu_count() or 2))
    started = time.time()

    if args.command == "fetch-toolchain":
        sys.path.insert(0, str(ROOT / "scripts"))
        from fetch_toolchain import install as install_toolchain
        return install_toolchain()
    if args.command == "doctor":
        return doctor(resolve_cross(args.cross), args.build_dir)
    if args.command == "clean":
        return clean(build, out)

    cross = resolve_cross(args.cross)
    if not cross and args.fetch:
        print("toolchain missing - downloading the pinned build")
        sys.path.insert(0, str(ROOT / "scripts"))
        from fetch_toolchain import install as install_toolchain
        try:
            install_toolchain()
        except SystemExit as exc:
            print("toolchain download failed: %s" % exc, file=sys.stderr)
            return 1
        cross = resolve_cross(args.cross)
    try:
        if args.command == "sim":
            return build_sim(build, args.verbose, args.host_cc)
        if args.command in ("tests", "test"):
            return build_tests(build, args.verbose, args.host_cc, True)

        if not cross:
            py = "py" if IS_WINDOWS else "python3"
            sep = "\\" if IS_WINDOWS else "/"
            print("error: no arm-none-eabi toolchain found.\n"
                  "\n"
                  "  fix it in one step (any OS, ~290 MB, verified download):\n"
                  "      " + py + " scripts" + sep + "build.py all --fetch\n"
                  "\n"
                  "  or install it explicitly:\n"
                  "      windows     : fetch-toolchain.cmd\n"
                  "      linux/macos : ./toolchain/fetch-toolchain.sh\n"
                  "\n"
                  "  already have arm-none-eabi-gcc? point the build at it:\n"
                  "      --cross C:\\msys64\\mingw64\\bin"
                  "   (or ...\\bin\\arm-none-eabi-gcc.exe)",
                  file=sys.stderr)
            return TOOLCHAIN_MISSING_EXIT

        if args.command in ("all", "boot"):
            build_boot(cross, build, jobs, args.verbose)
        if args.command in ("all", "kernel"):
            build_kernel(cross, build, jobs, args.verbose, args.platform)
        if args.command in ("all", "services"):
            build_services(cross, build, jobs, args.verbose)
        if args.command in ("all", "image"):
            build_image(build, out, args.esp_size, args.data_size,
                        args.install_size, args.verbose, args.platform)
    except BuildError as exc:
        print("\nbuild failed: %s" % exc, file=sys.stderr)
        return 1

    if args.command == "all":
        print("\ndone in %.1f s" % (time.time() - started))
    return 0


if __name__ == "__main__":
    sys.exit(main())
