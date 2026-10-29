#!/usr/bin/env python3
"""Windows host: package an immutable COPY of pristine ARM64 Setup WIM index 2.

Requires wimlib-imagex, WSL python3/hivex, and the already built degas logger.
Optional --driver-package stages existing INF/CAT/SYS files for later work;
it does not install a driver, trust a certificate, or enable test signing.
No phone, UEFI, BCD, host registry, or original WIM changes.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import shutil
import stat
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/degas/winpe-log"
SOURCE = ROOT / "artifacts/oneplus12/windows-boot-inputs/wim/boot.wim"
BASE_HASH = "2490c9c9afdb6e7c3e54ec5653f69b8be3465a2f0e178bf475daf3c005c5a5fc"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def arm64_pe(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"not a PE file: {path}")
    pe = struct.unpack_from("<I", data, 60)[0]
    if pe + 6 > len(data) or data[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", data, pe+4)[0] != 0xaa64:
        raise ValueError(f"not ARM64 PE: {path}")


def wsl_path(path):
    path = path.resolve()
    if not path.drive or path.drive.startswith("\\"):
        raise ValueError("WSL helper requires a local drive path")
    return "/mnt/" + path.drive[0].lower() + "/" + "/".join(path.parts[1:])


def helper(*args):
    command = ["wsl.exe", "--exec", "/usr/bin/python3",
               wsl_path(ROOT / "tools/prepare_degas_ufs_hive.py")]
    command += [wsl_path(arg) if isinstance(arg, Path) else arg for arg in args]
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver-package", type=Path,
                        help="stage an existing INF/CAT/SYS directory only; no install")
    parser.add_argument("--touch-package", type=Path,
                        help="optional signed DGS0002 INF/CAT/SYS/CER package; staged for drvload")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("run with Windows Python; hivex is invoked separately in WSL")
    if digest(SOURCE) != BASE_HASH:
        raise ValueError("source WIM hash mismatch")
    with SOURCE.open("rb") as stream:
        if struct.unpack_from("<I", stream.read(128), 120)[0] != 2:
            raise ValueError("expected source WIM boot index 2")
    exe = OUT / "degaslog-arm64.exe"
    arm64_pe(exe)
    exe_hash = digest(exe)
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S")
    work = OUT / ("package-" + stamp + "-" + exe_hash[:12])
    work.mkdir(parents=True, exist_ok=False)
    payload = work / "payload"
    payload.mkdir()
    with (work / "selftest-x64.txt").open("wb") as stdout, (work / "selftest-x64-stderr.txt").open("wb") as stderr:
        subprocess.run([str(OUT / "degaslog-x64.exe"), "/selftest"],
                       check=True, timeout=90, stdout=stdout, stderr=stderr,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    shutil.copyfile(exe, payload / "degaslog.exe")
    (payload / "winpeshl.ini").write_bytes(
        b"[LaunchApp]\r\nAppPath = %SYSTEMROOT%\\System32\\degaslog.exe\r\n")
    hive_dir = work / "hive"
    hive_dir.mkdir()
    subprocess.run(["wimlib-imagex", "extract", str(SOURCE), "2",
                    "Windows/System32/config/SYSTEM", f"--dest-dir={hive_dir}"], check=True)
    system = hive_dir / "SYSTEM"
    edited = hive_dir / "SYSTEM-DEGAS"
    helper(system, edited)
    policy = json.loads(Path(str(edited) + ".json").read_text())
    if not policy["source_unchanged"] or policy["readback"] != "passed":
        raise AssertionError("offline policy verification failed")
    entries = [(payload / "degaslog.exe", "Windows/System32/degaslog.exe"),
               (payload / "winpeshl.ini", "Windows/System32/winpeshl.ini"),
               (edited, "Windows/System32/config/SYSTEM")]
    driver_files = {}
    if args.driver_package:
        package = args.driver_package.resolve()
        files = sorted(p for p in package.iterdir() if p.is_file())
        if not files or any(p.suffix.lower() not in {".inf", ".cat", ".sys", ".cer"} for p in files):
            raise ValueError("driver staging accepts INF/CAT/SYS and optional CER files only")
        if not {".inf", ".cat", ".sys"}.issubset({p.suffix.lower() for p in files}):
            raise ValueError("driver package must contain INF, CAT and SYS")
        names = [p.name.casefold() for p in files]
        if len(names) != len(set(names)):
            raise ValueError("case-insensitive duplicate driver filenames")
        staged = payload / "DegasUfs"
        staged.mkdir()
        for path in files:
            if path.suffix.lower() == ".sys":
                arm64_pe(path)
            destination = staged / path.name
            shutil.copyfile(path, destination)
            driver_files[path.name] = digest(destination)
            entries.append((destination, "Windows/System32/DegasUfs/" + path.name))
    touch_files = {}
    if args.touch_package:
        package = args.touch_package.resolve()
        files = sorted(p for p in package.iterdir() if p.is_file() and p.name != "package.json")
        expected = {"degastouch.inf", "degastouch.cat", "degastouch.sys", "development.cer",
                    "degasvhf.inf", "degasvhf.cat", "mshidkmdf.sys"}
        if {p.name.casefold() for p in files} != expected or len(files) != len(expected):
            raise ValueError("touch package must contain degastouch INF/CAT/SYS, degasvhf INF/CAT, mshidkmdf.sys and development.cer")
        manifest = json.loads((package / "package.json").read_text())
        if manifest.get("target") != "ACPI\\DGS0002" or manifest.get("files") != {p.name: digest(p) for p in files}:
            raise ValueError("touch package manifest or hashes mismatch")
        cert = next(p for p in files if p.name.casefold() == "development.cer")
        if digest(cert) != "0515a9334e3dfc3c4a90a52dfc8a5fc6a0e995d199d77e92a1a90f3ef37920d6":
            raise ValueError("touch development certificate does not match the runtime pin")
        inf = next(p for p in files if p.name.casefold() == "degastouch.inf")
        inf_text = inf.read_text(encoding="utf-8-sig")
        if "ACPI\\DGS0002" not in inf_text or "LowerFilters" not in inf_text or '"vhf"' not in inf_text:
            raise ValueError("touch INF must bind DGS0002 and include the VHF lower filter")
        staged = payload / "DegasTouch"
        staged.mkdir()
        for path in files:
            if path.suffix.lower() == ".sys":
                arm64_pe(path)
            destination = staged / path.name
            shutil.copyfile(path, destination)
            touch_files[path.name] = digest(destination)
            entries.append((destination, "Windows/System32/DegasTouch/" + path.name))
    candidate = OUT / ("boot-degas-inventory-" + stamp + "-" + exe_hash[:12] + ".wim")
    if candidate.exists() or candidate.resolve() == SOURCE.resolve():
        raise FileExistsError(candidate)
    shutil.copyfile(SOURCE, candidate)
    candidate.chmod(stat.S_IREAD | stat.S_IWRITE)
    commands = "".join(f'add "{path}" "/{target}"\n' for path, target in entries)
    (work / "wim-update.txt").write_text(commands)
    subprocess.run(["wimlib-imagex", "update", str(candidate), "2", "--check", "--threads=4"],
                   input=commands, text=True, check=True)
    subprocess.run(["wimlib-imagex", "verify", str(candidate)], check=True)
    verify = work / "readback"
    verify.mkdir()
    # Separate destinations prevent flattening collisions in optional packages.
    checked = {}
    for number, (path, target) in enumerate(entries):
        folder = verify / str(number)
        folder.mkdir()
        subprocess.run(["wimlib-imagex", "extract", str(candidate), "2", target,
                        f"--dest-dir={folder}"], check=True)
        readback = folder / Path(target).name
        if digest(readback) != digest(path):
            raise AssertionError(f"WIM readback mismatch: {target}")
        checked[target] = digest(readback)
        if target.endswith("/config/SYSTEM"):
            helper("--verify", readback)
    with candidate.open("rb") as stream:
        if struct.unpack_from("<I", stream.read(128), 120)[0] != 2:
            raise AssertionError("candidate boot index changed")
    if digest(SOURCE) != BASE_HASH or digest(exe) != exe_hash:
        raise AssertionError("source WIM or logger changed during packaging")
    report = {"source": str(SOURCE), "source_sha256": BASE_HASH,
              "source_unchanged": True, "candidate": str(candidate),
              "candidate_name": candidate.name, "candidate_sha256": digest(candidate),
              "candidate_bytes": candidate.stat().st_size, "boot_index": 2,
              "arm64_exe_sha256": exe_hash,
              "logger_source_sha256": digest(ROOT / "diagnostic/degas/winpe-log/degaslog.c"),
              "packager_sha256": digest(Path(__file__)),
              "hive_helper_sha256": digest(ROOT / "tools/prepare_degas_ufs_hive.py"),
              "x64_selftest": "passed", "wim_verify": "passed", "readback": checked,
              "offline_policy": policy, "automatic_setup": False,
              "startup": "%SYSTEMROOT%\\System32\\degaslog.exe; driver binding before wpeinit; boot injection pending",
              "log_destination": "RAM X:\\DegasLogs plus two CRC-protected 64KiB raw slots at LUN2 LBA29800448",
              "journal_mode": "raw-two-slot-v1", "driver_files_staged_only": driver_files,
              "touch_files_staged_only": touch_files,
              "touch_installation": bool(touch_files),
              "driver_installation": bool(driver_files), "test_signing_enabled": False,
              "runtime_binding": "ARM64 MiniNT only, exact DGS0001, SAN3, NoAutoMount1, pinned test certificate; drvload into PE RAM",
              "host_registry_modified": False, "phone_accessed": False,
              "uefi_modified": False, "bcd_modified": False,
              "limits": "Offline verification only. SAN3/NoAutoMount reduce mounting; they are not a hardware write blocker."}
    content = json.dumps(report, indent=2) + "\n"
    (work / "manifest.json").write_text(content)
    (OUT / "package.json").write_text(content)
    print(content)


if __name__ == "__main__":
    main()
