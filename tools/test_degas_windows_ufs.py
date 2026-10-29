#!/usr/bin/env python3
"""Run the actual degas miniport request/DMA functions with a mock Storport/HCI.

Windows hosts invoke Linux compilers through WSL. No WDK/driver build, registry,
WIM, or device access. Generated C and sanitizer binaries stay in artifacts.
The mocks validate logic and bounds, not the Windows ABI or hardware DMA.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "diagnostic/degas/ufs/windows/degasufs.c"
HARNESS = ROOT / "diagnostic/tests/test_degas_windows_ufs.c"
RETAINED_SOURCE = ROOT / "diagnostic/degas/ufs/windows/retained_log.h"
RETAINED_HARNESS = ROOT / "diagnostic/tests/test_degas_retained_log.c"
OUT = ROOT / "artifacts/degas/ufs-windows/host-tests"
FUNCTIONS = ("Rd", "Wr", "Zero", "Copy", "Be32", "Be64", "Put32", "Stop",
             "Complete", "Sense", "Allowed", "Send", "TransportDone", "Poll",
             "BuildIo", "StartIo")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract_function(text, name):
    match = re.search(r"^static[^\n;]*\b" + re.escape(name) + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise ValueError(f"cannot find function definition: {name}")
    # Ignore braces in strings and comments when finding the function boundary.
    token = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
    depth = 0
    for part in token.finditer(text, match.end()-1):
        if part.group() == "{":
            depth += 1
        elif part.group() == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():part.end()]
    raise ValueError(f"unterminated function: {name}")


def linux_path(path):
    path = path.resolve()
    if os.name == "nt":
        return "/mnt/" + path.drive[0].lower() + "/" + "/".join(path.parts[1:])
    return str(path)


def run(command, **kwargs):
    if os.name == "nt":
        command = ["wsl.exe", "--exec", *command]
    return subprocess.run(command, text=True, capture_output=True, **kwargs)


def compile_run(compiler, folder, harness=HARNESS, defines=()):
    binary = folder / (compiler + "-" + harness.stem)
    command = [compiler, *defines, "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
               "-Wno-misleading-indentation", "-fno-omit-frame-pointer",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-I", linux_path(folder), linux_path(harness), "-o", linux_path(binary)]
    build = run(command, timeout=90)
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    result = run(["env", "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1",
                  "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1", linux_path(binary)], timeout=30)
    return {"compiler": compiler, "defines": list(defines), "exit_code": result.returncode,
            "stdout": result.stdout.strip(), "stderr": result.stderr.strip()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutations", action="store_true",
                        help="also prove rejection of two deliberately broken extracted copies")
    args = parser.parse_args()
    raw = SOURCE.read_bytes()
    retained_raw = RETAINED_SOURCE.read_bytes()
    source = raw.decode("utf-8")
    constants = re.findall(r"^#define (?:HCI_PA|HCI_SIZE|DMA_SIZE|MAX_DATA|FATAL_IS)\b[^\n]*", source, re.M)
    declarations = re.search(r"typedef struct \{ PVOID Buffer;[\s\S]*?\} ADAPTER;", source)
    if len(constants) != 5 or not declarations:
        raise ValueError("miniport declarations changed; update extraction explicitly")
    functions = {name: extract_function(source, name) for name in FUNCTIONS}
    generated = "/* Extracted verbatim from degasufs.c; do not edit. */\n#ifndef DEGAS_UFS_PERFORMANCE\n#define DEGAS_UFS_PERFORMANCE 0\n#endif\n" + "\n".join(constants)
    generated += "\n" + declarations.group() + "\n" + "\n\n".join(functions.values()) + "\n"
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "degasufs-under-test.inc").write_text(generated)
    (OUT / "retained-log-under-test.inc").write_bytes(retained_raw)
    results = [compile_run(compiler, OUT) for compiler in ("gcc", "clang")]
    retained_results = [compile_run(compiler, OUT, RETAINED_HARNESS) for compiler in ("gcc", "clang")]
    for performance, install in ((0,1),(1,0),(1,1)):
        for compiler in ("gcc", "clang"):
            results.append(compile_run(compiler, OUT, defines=(
                "-DDEGAS_UFS_PERFORMANCE="+str(performance),
                "-DDEGAS_UFS_INSTALL_MODE="+str(install))))
    for result in results + retained_results:
        if result["exit_code"]:
            raise AssertionError(json.dumps(result, indent=2))
        print(result["compiler"] + ": " + result["stdout"])
    mutations = []
    if args.mutations:
        changes = {
            "unpadded-prdt": ("p[3]=((bytes+3)&~3u)-1", "p[3]=bytes-1"),
            "allow-write10": ("switch(c[0]){", "switch(c[0]){case 0x2a:return TRUE;"),
        }
        for name, (old, new) in changes.items():
            if generated.count(old) != 1:
                raise AssertionError(f"mutation anchor changed: {name}")
            folder = OUT / name
            folder.mkdir(exist_ok=True)
            (folder / "degasufs-under-test.inc").write_text(generated.replace(old, new))
            result = compile_run("clang", folder)
            if not result["exit_code"] or "CHECK failed" not in result["stderr"]:
                raise AssertionError(f"test did not reject mutation: {name}: {result}")
            mutations.append({"name": name, "rejected": True,
                              "failure": result["stderr"].splitlines()[0]})
            print("mutation rejected: " + name)
    if digest(SOURCE.read_bytes()) != digest(raw):
        raise AssertionError("miniport changed while testing; rerun against the current source")
    if digest(RETAINED_SOURCE.read_bytes()) != digest(retained_raw):
        raise AssertionError("retained header changed while testing; rerun")
    report = {"passed": True, "source_sha256": digest(raw),
              "harness_sha256": digest(HARNESS.read_bytes()),
              "retained_source_sha256": digest(retained_raw),
              "retained_harness_sha256": digest(RETAINED_HARNESS.read_bytes()),
              "functions_sha256": {name: digest(body.encode()) for name, body in functions.items()},
              "function_logic": "extracted verbatim; no duplicated driver implementations",
              "sanitizers": ["AddressSanitizer", "UndefinedBehaviorSanitizer"],
              "results": results, "retained_results": retained_results, "mutations": mutations,
              "limits": "Mock Storport/HCI only; not Windows ABI, DMA coherency, signing, or hardware qualification",
              "device_accessed": False, "production_files_modified": False}
    (OUT / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Report: " + str(OUT / "report.json"))


if __name__ == "__main__":
    main()
