"""Generate the bounded OnePlus 12 ACPI 6.3 diagnostic table set.

Hardware values come only from evidence/oneplus12/acpi/platform-evidence.json.
Each GICC carries its evidence-backed per-CPU redistributor base.  The MADT
contains no GICR discovery structure, so OSPM uses those explicit GICC fields
instead of treating the redistributors as one dense discovery range.
The generated GTDT exposes only architected timers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EVIDENCE = ROOT / "evidence/oneplus12/acpi/platform-evidence.json"
DEFAULT_HEADER = ROOT / "mu/Platforms/OnePlus/wafflePkg/Drivers/Op12AcpiDxe/AcpiTablesGenerated.h"
DEFAULT_OUTPUT = ROOT / "artifacts/oneplus12/acpi-tables"
DEFAULT_IASL = "/home/gavso/op12-acpica/usr/bin/iasl"

OEM_ID = b"OP12  "
OEM_TABLE_ID = b"WAFFLE  "
OEM_REVISION = 1
CREATOR_ID = b"PYTH"
CREATOR_REVISION = 1
ACPI_HEADER = struct.Struct("<4sIBB6s8sI4sI")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def repo_path(path):
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path.resolve())


def wsl_path(path):
    resolved = path.resolve()
    drive = resolved.drive
    if len(drive) != 2 or drive[1] != ":":
        raise ValueError(f"cannot convert path to WSL form: {resolved}")
    tail = resolved.as_posix()[2:].lstrip("/")
    return f"/mnt/{drive[0].lower()}/{tail}"


def run_iasl(iasl, arguments, cwd):
    command = ["wsl", iasl, *arguments]
    completed = subprocess.run(command, cwd=cwd, text=True, encoding="utf-8", errors="replace",
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    if completed.returncode:
        raise RuntimeError(f"iasl failed ({completed.returncode}): {' '.join(command)}\n{completed.stdout}")
    return completed.stdout


def table(signature, revision, body):
    length = ACPI_HEADER.size + len(body)
    header = ACPI_HEADER.pack(signature, length, revision, 0, OEM_ID, OEM_TABLE_ID,
                              OEM_REVISION, CREATOR_ID, CREATOR_REVISION)
    data = bytearray(header + body)
    data[9] = (-sum(data)) & 0xff
    assert len(data) == length and sum(data) & 0xff == 0
    return bytes(data)


def zero_gas():
    return struct.pack("<BBBBQ", 0, 0, 0, 0, 0)


def build_fadt(evidence):
    psci = evidence["psci"]
    if psci["compatible"] != ["arm,psci-1.0"] or psci["method"] != "smc":
        raise ValueError("evidence does not declare PSCI 1.0 with the SMC conduit")
    body = b"".join([
        struct.pack("<II", 0, 0),                       # FirmwareCtrl, Dsdt
        struct.pack("<BBH", 0, 0, 0),                  # Reserved, profile, SCI
        struct.pack("<I", 0),                           # SMI command
        struct.pack("<BBBB", 0, 0, 0, 0),
        struct.pack("<8I", *([0] * 8)),                 # legacy PM/GPE blocks
        struct.pack("<8B", *([0] * 8)),                 # legacy PM/GPE lengths
        struct.pack("<4H", *([0] * 4)),                 # latencies/flush geometry
        struct.pack("<5B", *([0] * 5)),
        struct.pack("<H", 0),                           # IA-PC boot arch
        struct.pack("<B", 0),                           # Reserved
        struct.pack("<I", 1 << 20),                     # HW_REDUCED_ACPI
        zero_gas(),                                      # Reset register
        struct.pack("<BHB", 0, 1, 3),                   # Reset value, PSCI SMC, minor 3
        struct.pack("<QQ", 0, 0),                       # XFirmwareCtrl, XDsdt
        b"".join(zero_gas() for _ in range(8)),          # extended PM/GPE blocks
        zero_gas(),                                      # Sleep control register unknown
        zero_gas(),                                      # Sleep status register unknown
        struct.pack("<Q", 0),                           # hypervisor identity
    ])
    result = table(b"FACP", 6, body)
    if len(result) != 276:
        raise AssertionError(f"FADT size is {len(result)}, expected 276")
    return result


def build_madt(evidence):
    cpus = evidence["cpu"]["cpus"]
    if len(cpus) != 8 or any(cpu["status"]["effective"] != "okay" for cpu in cpus):
        raise ValueError("expected exactly eight effective-enabled CPUs")
    maintenance = evidence["gic"]["maintenance_interrupt"]
    if maintenance["kind"] != "PPI" or maintenance["gic_intid"] is None:
        raise ValueError("GIC maintenance interrupt is not a decoded PPI")
    gicc_format = struct.Struct("<BBHIIIIIQQQQIQQBBH")
    if gicc_format.size != 80:
        raise AssertionError("ACPI 6.3 GICC structure size mismatch")
    redist = evidence["gic"]["redistributor_region"]
    redist_base = int(redist["address"], 0)
    redist_size = int(redist["size"], 0)
    redist_stride = int(evidence["gic"]["redistributor_stride"], 0)
    if len(cpus) != evidence["gic"]["redistributor_slots_by_range_geometry"]:
        raise ValueError("CPU count does not match GICR slot geometry")
    if (len(cpus) - 1) * redist_stride >= redist_size:
        raise ValueError("per-CPU GICR bases exceed the DT redistributor region")
    entries = []
    for uid, cpu in enumerate(cpus):
        mpidr = int(cpu["dt_declared_mpidr"], 0)
        entries.append(gicc_format.pack(
            0x0b, 80, 0,       # type, length, reserved
            0,                 # CPU interface number: GICv3 compatibility mode not asserted
            uid,               # matches DSDT _UID
            1,                 # enabled
            0, 0,              # parking version, performance interrupt
            0, 0, 0, 0,       # parked address, GICC, GICV, GICH
            maintenance["gic_intid"],
            redist_base + uid * redist_stride,
            mpidr,
            0, 0, 0,           # efficiency class, reserved, SPE overflow
        ))
    distributor = evidence["gic"]["distributor"]
    gicd = struct.pack("<BBHIQIB3s", 0x0c, 24, 0, 0,
                       int(distributor["address"], 0), 0, 3, b"\0\0\0")
    body = struct.pack("<II", 0, 0) + b"".join(entries) + gicd
    result = table(b"APIC", 5, body)
    if len(result) != 44 + 8 * 80 + 24:
        raise AssertionError("MADT size mismatch")
    return result


def build_gtdt(evidence):
    timer = evidence["timers"]["architected"]
    interrupts = timer["interrupts"]
    roles = ["secure physical", "non-secure physical", "virtual", "hypervisor"]
    if len(interrupts) != 4 or [item["role_from_binding_order"] for item in interrupts] != roles:
        raise ValueError("unexpected architected timer interrupt ordering")
    if any(item["gic_intid"] is None for item in interrupts):
        raise ValueError("architected timer interrupt is unresolved")
    flags = []
    for item in interrupts:
        acpi_flags = 0
        if item["trigger"].startswith("edge-"):
            acpi_flags |= 1
        if item["trigger"].endswith("-low"):
            acpi_flags |= 2
        if timer["always_on"]:
            acpi_flags |= 4
        flags.append(acpi_flags)
    body = struct.pack(
        "<Q9IQ4I",
        0xffffffffffffffff,     # optional counter control frame not selected
        0,                      # reserved
        interrupts[0]["gic_intid"], flags[0],
        interrupts[1]["gic_intid"], flags[1],
        interrupts[2]["gic_intid"], flags[2],
        interrupts[3]["gic_intid"], flags[3],
        0xffffffffffffffff,     # optional counter read frame not selected
        0, 0,                   # no platform timer structures
        0, 0,                   # virtual EL2 timer is not present in the evidence
    )
    result = table(b"GTDT", 3, body)
    if len(result) != 104:
        raise AssertionError(f"GTDT size is {len(result)}, expected 104")
    return result


def build_dbg2():
    """Build the vetted single-GAS Qualcomm/Synopsys EEM debug descriptor."""
    # Debug-device header (22), one GAS (12), address size (4), namespace (2),
    # and OEM selector (2).  Selector UINT16 5 selects the Synopsys path in
    # kd_8003_5143.dll; no donor FIX/FIX2 register-write payload is present.
    device = struct.pack(
        "<BHBHHHHHHHHH",
        0, 42, 1, 2, 38, 2, 40,
        0x8003, 0x5143, 0, 22, 34,
    )
    device += struct.pack("<BBBBQ", 0, 32, 0, 3, 0x0A600000)
    device += struct.pack("<I", 0x100000) + b".\0" + struct.pack("<H", 5)
    result = table(b"DBG2", 0, struct.pack("<II", 44, 1) + device)
    if len(result) != 86:
        raise AssertionError(f"DBG2 size is {len(result)}, expected 86")
    return result


def dsdt_source():
    devices = []
    for uid in range(8):
        devices.append(f"""        Device (C{uid:03d})
        {{
            Name (_HID, \"ACPI0007\")
            Name (_UID, 0x{uid:08X})
        }}""")
    return """/* Generated from the bounded OnePlus 12 ACPI evidence. */
DefinitionBlock (\"\", \"DSDT\", 2, \"OP12\", \"WAFFLE\", 0x00000001)
{
    Scope (\\_SB)
    {
""" + "\n\n".join(devices) + """
    }
}
"""


def validate_table(data, signature, revision, length):
    if len(data) != length:
        raise ValueError(f"{signature.decode()} length {len(data)} != {length}")
    parsed = ACPI_HEADER.unpack_from(data)
    if parsed[0] != signature or parsed[1] != length or parsed[2] != revision:
        raise ValueError(f"invalid {signature.decode()} header")
    if sum(data) & 0xff:
        raise ValueError(f"invalid {signature.decode()} checksum")


def header_array(name, data):
    lines = [f"STATIC CONST UINT8  {name}[] = {{"]
    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        lines.append("  " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def write_header(path, binaries, evidence_hash, enable_dbg2=False):
    text = """// SPDX-License-Identifier: BSD-2-Clause-Patent
// Generated by tools/generate_op12_acpi.py; do not edit.
// Source evidence SHA-256: %s

#ifndef OP12_ACPI_TABLES_GENERATED_H_
#define OP12_ACPI_TABLES_GENERATED_H_

#ifndef OP12_ACPI_ENABLE_DBG2
#define OP12_ACPI_ENABLE_DBG2 %d
#elif OP12_ACPI_ENABLE_DBG2 != %d
#error "Regenerate AcpiTablesGenerated.h with matching DBG2 setting"
#endif

%s

%s

%s

%s

%s

#endif
""" % (
        evidence_hash,
        1 if enable_dbg2 else 0,
        1 if enable_dbg2 else 0,
        header_array("mOp12Dsdt", binaries["Dsdt"]),
        header_array("mOp12Fadt", binaries["Fadt"]),
        header_array("mOp12Madt", binaries["Madt"]),
        header_array("mOp12Gtdt", binaries["Gtdt"]),
        header_array("mOp12Dbg2", binaries["Dbg2"]),
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def validate_disassembly(output_dir):
    checks = {
        "Dsdt.dsl": ["Device (C000)", '"ACPI0007"', "Name (_UID, 0x07)"],
        "Fadt.dsl": ["Hardware Reduced (V5) : 1", "ARM Flags", "FADT Minor Revision : 03"],
        "Madt.dsl": ["[Generic Interrupt Controller]", "[Generic Interrupt Distributor]"],
        "Gtdt.dsl": ["Secure EL1 Interrupt", "Non-Secure EL1 Interrupt", "Virtual Timer Interrupt"],
        "Dbg2.dsl": ["Info Count : 00000001", "Register Count : 01",
                     "Port Type : 8003", "Port Subtype : 5143",
                     "Address : 000000000A600000", "Address Size : 00100000"],
    }
    for filename, needles in checks.items():
        text = (output_dir / filename).read_text(encoding="utf-8", errors="replace")
        missing = [needle for needle in needles if needle not in text]
        if missing:
            raise ValueError(f"{filename} lacks expected disassembly fields: {missing}")
        if filename == "Madt.dsl" and "[Generic Interrupt Redistributor]" in text:
            raise ValueError("Madt.dsl unexpectedly contains a GICR discovery structure")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    ap.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--iasl", default=DEFAULT_IASL)
    ap.add_argument("--enable-dbg2", action="store_true",
                    help="include the vetted one-GAS EEM DBG2 in Op12AcpiDxe (default: off)")
    args = ap.parse_args()

    evidence_bytes = args.evidence.read_bytes()
    evidence = json.loads(evidence_bytes)
    if evidence.get("schema") != "op12-acpi-evidence-v1":
        raise ValueError("unsupported evidence schema")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    dsdt_asl = args.output_dir / "Dsdt.asl"
    dsdt_aml = args.output_dir / "Dsdt.aml"
    dsdt_asl.write_text(dsdt_source(), encoding="utf-8", newline="\n")
    compile_output = run_iasl(args.iasl, ["-tc", "-p", wsl_path(args.output_dir / "Dsdt"), wsl_path(dsdt_asl)], args.output_dir)
    dsdt = dsdt_aml.read_bytes()

    binaries = {
        "Dsdt": dsdt,
        "Fadt": build_fadt(evidence),
        "Madt": build_madt(evidence),
        "Gtdt": build_gtdt(evidence),
        "Dbg2": build_dbg2(),
    }
    validate_table(binaries["Dsdt"], b"DSDT", 2, len(binaries["Dsdt"]))
    validate_table(binaries["Fadt"], b"FACP", 6, 276)
    validate_table(binaries["Madt"], b"APIC", 5, 708)
    validate_table(binaries["Gtdt"], b"GTDT", 3, 104)
    validate_table(binaries["Dbg2"], b"DBG2", 0, 86)

    binary_paths = {
        "Dsdt": dsdt_aml,
        "Fadt": args.output_dir / "Fadt.bin",
        "Madt": args.output_dir / "Madt.bin",
        "Gtdt": args.output_dir / "Gtdt.bin",
        "Dbg2": args.output_dir / "Dbg2.bin",
    }
    for name in ("Fadt", "Madt", "Gtdt", "Dbg2"):
        binary_paths[name].write_bytes(binaries[name])

    disassembly_logs = []
    for name, binary_path in binary_paths.items():
        prefix = args.output_dir / name
        dsl_path = prefix.with_suffix(".dsl")
        if dsl_path.exists():
            dsl_path.unlink()
        output = run_iasl(args.iasl, ["-d", "-p", wsl_path(prefix), wsl_path(binary_path)], args.output_dir)
        disassembly_logs.append(f"[{name}]\n{output.rstrip()}\n")
    validate_disassembly(args.output_dir)

    evidence_hash = sha256(evidence_bytes)
    write_header(args.header, binaries, evidence_hash, args.enable_dbg2)
    (args.output_dir / "iasl-compile.log").write_text(compile_output, encoding="utf-8", newline="\n")
    (args.output_dir / "iasl-disassemble.log").write_text("\n".join(disassembly_logs), encoding="utf-8", newline="\n")

    version = run_iasl(args.iasl, ["-v"], args.output_dir).strip()
    artifacts = []
    for path in sorted(args.output_dir.iterdir(), key=lambda item: item.name.lower()):
        if path.is_file() and path.name != "manifest.json":
            data = path.read_bytes()
            artifacts.append({"path": repo_path(path), "size_bytes": len(data), "sha256": sha256(data)})
    header_bytes = args.header.read_bytes()
    manifest = {
        "schema": "op12-acpi-tables-v1",
        "generator": {"path": repo_path(Path(__file__)), "sha256": sha256(Path(__file__).read_bytes())},
        "source_evidence": {
            "path": repo_path(args.evidence),
            "sha256": evidence_hash,
            "live_dtb_sha256": evidence["sources"]["live_dtb"]["sha256"],
        },
        "iasl": {"executable": args.iasl, "version_output": version},
        "header": {"path": repo_path(args.header), "size_bytes": len(header_bytes), "sha256": sha256(header_bytes)},
        "tables": {
            "DSDT": {"length": len(dsdt), "revision": 2, "cpu_uids": list(range(8))},
            "FADT": {"length": 276, "revision": 6, "minor_revision": 3, "dsdt": 0, "x_dsdt": 0,
                     "hw_reduced": True, "psci_compliant": True, "psci_use_hvc": False},
            "MADT": {"length": 708, "revision": 5, "gicc_length": 80,
                     "mpidrs": [cpu["dt_declared_mpidr"] for cpu in evidence["cpu"]["cpus"]],
                     "gicd_base": evidence["gic"]["distributor"]["address"],
                     "gicr_stride": evidence["gic"]["redistributor_stride"],
                     "gicc_gicr_bases": [
                         hex(int(evidence["gic"]["redistributor_region"]["address"], 0) +
                             slot * int(evidence["gic"]["redistributor_stride"], 0))
                         for slot in range(len(evidence["cpu"]["cpus"]))
                     ],
                     "gicr_discovery_structure_count": 0,
                     "gicr_affinity_mapping": "one explicit GICC GICR base per runtime-validated DT stride slot"},
            "GTDT": {"length": 104, "revision": 3,
                     "architected_timer_gsivs": [item["gic_intid"] for item in evidence["timers"]["architected"]["interrupts"]],
                     "platform_timer_count": 0},
            "DBG2": {"length": 86, "revision": 0, "enabled": args.enable_dbg2,
                     "port_type": "0x8003", "port_subtype": "0x5143",
                     "gas_count": 1, "gas_address": "0x0a600000",
                     "gas_address_size": "0x100000", "oem_selector_uint16": 5,
                     "fixup_payload_bytes": 0},
        },
        "artifacts": artifacts,
        "validation": [
            "all binary ACPI checksums sum to zero",
            "binary headers, revisions, and fixed structure lengths checked by the generator",
            "DSDT compiled by iasl and every emitted binary independently disassembled by iasl",
            "disassemblies checked for the expected CPU, FADT, GIC, and timer fields",
        ],
        "disassembler_observations": [
            "iasl 20251212 displays the newer TRBE Interrupt extension after each 80-byte ACPI 6.3 GICC; the generated subtable Length is 0x50 and independent offset validation advances by that declared length",
        ],
        "unresolved_runtime_facts": evidence["unresolved_facts"],
    }
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    state = "enabled" if args.enable_dbg2 else "disabled"
    print(f"generated DSDT {len(dsdt)}, FADT 276, MADT 708, GTDT 104, DBG2 86 bytes ({state})")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
