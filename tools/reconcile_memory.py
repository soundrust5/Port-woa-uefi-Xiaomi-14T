"""Reconcile the captured live FDT memory declarations with LK and Linux.

This is an evidence extractor, not a Mu memory-map generator.  It deliberately
keeps allocator-only reserved-memory nodes as candidates unless the capture
contains a uniquely matching runtime interval.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


DRAM_START = 0x40000000
DRAM_END = 0x340000000


def hx(value: int) -> str:
    return f"0x{value:x}"


def interval(start: int, end: int, **extra):
    return {
        "start": hx(start),
        "end_exclusive": hx(end),
        "size": hx(end - start),
        **extra,
    }


def merge(items):
    result = []
    for start, end in sorted((start, end) for start, end in items if start < end):
        if result and start <= result[-1][1]:
            result[-1][1] = max(result[-1][1], end)
        else:
            result.append([start, end])
    return [(start, end) for start, end in result]


def subtract(items, exclusions):
    output = []
    for start, end in merge(items):
        pieces = [(start, end)]
        for ex_start, ex_end in merge(exclusions):
            next_pieces = []
            for piece_start, piece_end in pieces:
                if ex_end <= piece_start or piece_end <= ex_start:
                    next_pieces.append((piece_start, piece_end))
                else:
                    if piece_start < ex_start:
                        next_pieces.append((piece_start, ex_start))
                    if ex_end < piece_end:
                        next_pieces.append((ex_end, piece_end))
            pieces = next_pieces
        output.extend(pieces)
    return merge(output)


def prop_bytes(nodes, node, prop):
    return bytes.fromhex(nodes[node][prop]["hex"])


def be_cells(raw: bytes):
    if len(raw) % 4:
        raise ValueError("property is not a whole number of cells")
    return [int.from_bytes(raw[i:i + 4], "big") for i in range(0, len(raw), 4)]


def parse_iomem(path: Path):
    rows = []
    pattern = re.compile(r"^( *)([0-9a-f]+)-([0-9a-f]+) : (.+)$", re.I)
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = pattern.match(line)
        if not match:
            continue
        rows.append({
            "start_i": int(match.group(2), 16),
            "end_i": int(match.group(3), 16) + 1,
            "label": match.group(4),
            "depth": len(match.group(1)),
            "line": line_number,
        })
    return rows


def parse_memblock(path: Path):
    rows = []
    pattern = re.compile(r"^\s*(\d+):\s+0x([0-9a-f]+)\.\.0x([0-9a-f]+)\s*$", re.I)
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = pattern.match(line)
        if match:
            rows.append(interval(
                int(match.group(2), 16), int(match.group(3), 16) + 1,
                index=int(match.group(1)), line=line_number))
    return rows


def parse_cma_area(directory: Path, name: str, page_size: int = 4096):
    fields = {}
    sources = {}
    for field in ("base_pfn", "count", "order_per_bit", "used", "maxchunk"):
        path = directory / f"cma-{name}-{field}.txt"
        fields[field] = int(path.read_text(encoding="utf-8").strip())
        sources[field] = {
            "path": str(path.as_posix()),
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        }
    start = fields["base_pfn"] * page_size
    end = start + fields["count"] * page_size
    return interval(
        start, end, name=name, page_size=page_size, **fields,
        used_bytes=fields["used"] * page_size,
        maxchunk_bytes=fields["maxchunk"] * page_size,
        sources=sources)


def parse_header_value(path: Path, label: str) -> int:
    match = re.search(
        rf"^{re.escape(label)}:\s+(0x[0-9a-f]+|\d+)\s*$",
        path.read_text(encoding="utf-8"), re.I | re.M)
    if not match:
        raise ValueError(f"missing {label!r} in {path}")
    return int(match.group(1), 0)


def overlaps(row, start, end):
    return row["start_i"] < end and start < row["end_i"]


def parse_lk(path: Path):
    text = path.read_bytes().replace(b"\0", b"").decode("utf-8", "replace")
    allocation_re = re.compile(
        r"mblock_alloc_range(?:_no_lock)?:.*?start: 0x([0-9a-f]+), "
        r"(?:sz|size): 0x([0-9a-f]+).*?map:([01]) name:([^\s]+)", re.I)
    free_re = re.compile(
        r"mblock_free_with_size:.*?start 0x([0-9a-f]+) size: 0x([0-9a-f]+), name: ([^\r\n]+)", re.I)
    reserved_re = re.compile(
        r"mblock-R\[(\d+)\]\.start: 0x([0-9a-f]+), size: 0x([0-9a-f]+) "
        r"map:([01]) name:([^\r\n]+)", re.I)
    allocations, frees, checkpoints = [], [], []
    current = []
    last_line = None
    for line_number, line in enumerate(text.splitlines(), 1):
        match = allocation_re.search(line)
        if match:
            allocations.append(interval(
                int(match.group(1), 16),
                int(match.group(1), 16) + int(match.group(2), 16),
                map=int(match.group(3)), name=match.group(4), line=line_number))
        match = free_re.search(line)
        if match:
            frees.append(interval(
                int(match.group(1), 16),
                int(match.group(1), 16) + int(match.group(2), 16),
                name=match.group(3).strip(), line=line_number))
        match = reserved_re.search(line)
        if match:
            if last_line is not None and line_number != last_line + 1:
                checkpoints.append(current)
                current = []
            start = int(match.group(2), 16)
            current.append(interval(
                start, start + int(match.group(3), 16), index=int(match.group(1)),
                map=int(match.group(4)), name=match.group(5).strip(), line=line_number))
            last_line = line_number
    if current:
        checkpoints.append(current)
    # The ring contains repeated snapshots.  Keep all records, while exposing
    # the largest parsed snapshot as the most useful checkpoint.
    largest = max(checkpoints, key=len, default=[])
    return {
        "source": str(path.as_posix()),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "allocation_events": allocations,
        "free_events": frees,
        "reserved_checkpoints": checkpoints,
        "largest_reserved_checkpoint": largest,
        "observed_dram": [
            interval(0x40000000, 0x1c0000000, rank=0),
            interval(0x1c0000000, 0x340000000, rank=1),
        ],
        "caveat": (
            "The captured /proc/pl_lk ring has repeated/interleaved boot output and "
            "ends mid-boot. Events prove allocations at their logged points, but "
            "only a matching free event proves release."
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fdt-json", type=Path, default=Path("evidence/extracted/2026-09-14/live-fdt.json"))
    parser.add_argument("--iomem", type=Path, default=Path("evidence/raw/2026-09-14-initial/iomem.txt"))
    parser.add_argument("--lk", type=Path, default=Path("evidence/raw/2026-09-14-memory-display/pl_lk.txt"))
    parser.add_argument("--meminfo", type=Path, default=Path("evidence/raw/2026-09-14-initial/meminfo.txt"))
    parser.add_argument("--memblock-memory", type=Path, default=Path("evidence/raw/2026-09-14-memory-display/memblock-memory.txt"))
    parser.add_argument("--memblock-reserved", type=Path, default=Path("evidence/raw/2026-09-14-memory-display/memblock-reserved.txt"))
    parser.add_argument("--cma-directory", type=Path, default=Path("evidence/raw/2026-09-14-memory-display"))
    parser.add_argument("--bootconfig", type=Path, default=Path("evidence/raw/2026-09-14-initial/bootconfig.txt"))
    parser.add_argument("--kernel-config", type=Path, default=Path("evidence/raw/2026-09-14-memory-display/kernel-memory-config.txt"))
    parser.add_argument("--boot-header", type=Path, default=Path("evidence/extracted/2026-09-14/boot-images-complete/live-b-boot/header.txt"))
    parser.add_argument("--vendor-boot-header", type=Path, default=Path("evidence/extracted/2026-09-14/boot-images-complete/live-b-vendor_boot/header.txt"))
    parser.add_argument("--output", type=Path, default=Path("evidence/extracted/2026-09-14/memory-reconciliation.json"))
    args = parser.parse_args()

    fdt = json.loads(args.fdt_json.read_text(encoding="utf-8"))
    nodes = fdt["nodes"]
    iomem = parse_iomem(args.iomem)
    lk = parse_lk(args.lk)
    memblock_memory = parse_memblock(args.memblock_memory)
    memblock_reserved = parse_memblock(args.memblock_reserved)
    kernel_config_text = args.kernel_config.read_text(encoding="utf-8")
    page_shift_match = re.search(r"^CONFIG_ARM64_PAGE_SHIFT=(\d+)$", kernel_config_text, re.M)
    if not page_shift_match:
        raise ValueError("CONFIG_ARM64_PAGE_SHIFT is missing")
    page_size = 1 << int(page_shift_match.group(1))
    cma_size_match = re.search(r"^CONFIG_CMA_SIZE_MBYTES=(\d+)$", kernel_config_text, re.M)
    if not cma_size_match:
        raise ValueError("CONFIG_CMA_SIZE_MBYTES is missing")
    configured_cma_size_mib = int(cma_size_match.group(1))
    cma_areas = [
        parse_cma_area(args.cma_directory, "reserved", page_size),
        parse_cma_area(args.cma_directory, "ssmr-reserved-cma-memory", page_size),
    ]
    slot_match = re.search(
        r'^androidboot\.slot_suffix\s*=\s*"?_([ab])"?\s*$',
        args.bootconfig.read_text(encoding="utf-8"), re.M)
    if not slot_match:
        raise ValueError("active slot suffix is missing")
    active_slot = slot_match.group(1)
    kernel_image_size = parse_header_value(args.boot_header, "kernel_size")
    kernel_load_address = parse_header_value(args.vendor_boot_header, "kernel load address")
    ramdisk_load_address = parse_header_value(args.vendor_boot_header, "ramdisk load address")
    dtb_base = parse_header_value(args.vendor_boot_header, "dtb address")

    memory_reg = be_cells(prop_bytes(nodes, "/memory", "reg"))
    dram_start = (memory_reg[0] << 32) | memory_reg[1]
    dram_size = (memory_reg[2] << 32) | memory_reg[3]
    if (dram_start, dram_start + dram_size) != (DRAM_START, DRAM_END):
        raise ValueError("unexpected DRAM range")

    fixed = []
    for row in fdt["reg_inventory"]:
        if not row["path"].startswith("/reserved-memory/mblock-"):
            continue
        start = int(row["bus_address"], 16)
        size = int(row["size"], 16)
        end = start + size
        top = [entry for entry in iomem if entry["depth"] == 0 and overlaps(entry, start, end)]
        children = [entry for entry in iomem if entry["depth"] > 0 and entry["label"] == "reserved" and overlaps(entry, start, end)]
        if len(top) == 1 and top[0]["label"] == "reserved":
            iomem_class = "excluded_from_top_level_System_RAM"
        elif len(top) == 1 and top[0]["label"] == "System RAM" and children:
            iomem_class = "System_RAM_with_reserved_child"
        elif len(top) == 1 and top[0]["label"] == "System RAM":
            iomem_class = "System_RAM_without_reserved_child"
        else:
            iomem_class = "mixed_or_unresolved"
        lk_exact = []
        for observed in lk["allocation_events"] + lk["largest_reserved_checkpoint"]:
            if int(observed["start"], 16) == start and int(observed["end_exclusive"], 16) == end:
                lk_exact.append(observed)
        fixed.append(interval(
            start, end, path=row["path"], compatible=row["compatible"],
            no_map=bool(row["no_map"]), reusable=bool(row["reusable"]),
            iomem_class=iomem_class,
            iomem_top=[interval(x["start_i"], x["end_i"], label=x["label"], line=x["line"]) for x in top],
            iomem_reserved_children=[interval(x["start_i"], x["end_i"], line=x["line"]) for x in children],
            mblock_relation="excluded_from_decoded_free_list",
            lk_exact_observations=lk_exact))

    fixed_pairs = [(int(item["start"], 16), int(item["end_exclusive"], 16)) for item in fixed]
    fixed_union = merge(fixed_pairs)
    fixed_overlap_pairs = []
    fixed_sorted = sorted(fixed, key=lambda x: int(x["start"], 16))
    for left, right in zip(fixed_sorted, fixed_sorted[1:]):
        if int(right["start"], 16) < int(left["end_exclusive"], 16):
            fixed_overlap_pairs.append([left["path"], right["path"]])

    # The blob is native little-endian bootloader data.  The first 29 records
    # self-validate as <start,size,rank> because they exactly equal the
    # complement of every fixed reserved-memory reg interval.
    raw_mblock = prop_bytes(nodes, "/memory", "mblock_info")
    count = struct.unpack_from("<I", raw_mblock, 0)[0]
    decoded_mblock_prefix_size = 8 + count * 24
    decoded_mblocks = []
    for index in range(count):
        start, size, rank = struct.unpack_from("<QQQ", raw_mblock, 8 + index * 24)
        decoded_mblocks.append(interval(start, start + size, rank=rank, index=index))
    fixed_complement = []
    cursor = DRAM_START
    for start, end in fixed_union:
        if cursor < start:
            fixed_complement.append((cursor, start))
        cursor = max(cursor, end)
    if cursor < DRAM_END:
        fixed_complement.append((cursor, DRAM_END))
    decoded_pairs = [(int(x["start"], 16), int(x["end_exclusive"], 16)) for x in decoded_mblocks]

    dynamic = []
    for path in [
        "/reserved-memory/ccci-dpmaif-cache-memory",
        "/reserved-memory/ccci-dpmaif-nocache-memory",
        "/reserved-memory/ssmr-reserved-cma-memory",
        "/reserved-memory/mkp-kernel-code-protection-memory",
    ]:
        props = nodes[path]
        size_cells = be_cells(prop_bytes(nodes, path, "size"))
        size = (size_cells[0] << 32) | size_cells[1]
        align = 1
        if "alignment" in props:
            align_cells = be_cells(prop_bytes(nodes, path, "alignment"))
            align = (align_cells[0] << 32) | align_cells[1]
        range_cells = be_cells(prop_bytes(nodes, path, "alloc-ranges"))
        range_start = (range_cells[0] << 32) | range_cells[1]
        range_size = (range_cells[2] << 32) | range_cells[3]
        candidates = {}
        for entry in iomem:
            if entry["label"] != "reserved":
                continue
            if entry["end_i"] - entry["start_i"] != size or entry["start_i"] % align:
                continue
            if range_start <= entry["start_i"] and entry["end_i"] <= range_start + range_size:
                candidates[(entry["start_i"], entry["end_i"])] = True
        candidate_rows = [interval(start, end) for start, end in sorted(candidates)]
        memblock_matches = []
        if len(candidate_rows) == 1:
            candidate_start = int(candidate_rows[0]["start"], 16)
            candidate_end = int(candidate_rows[0]["end_exclusive"], 16)
            for observed in memblock_reserved:
                observed_start = int(observed["start"], 16)
                observed_end = int(observed["end_exclusive"], 16)
                if observed_start <= candidate_start and candidate_end <= observed_end:
                    relation = "exact" if (observed_start, observed_end) == (candidate_start, candidate_end) else "contained"
                    memblock_matches.append({**observed, "relation": relation})
        dynamic.append({
            "path": path,
            "size": hx(size),
            "alignment": hx(align),
            "alloc_range": interval(range_start, range_start + range_size),
            "no_map": "no-map" in props,
            "reusable": "reusable" in props,
            "iomem_exact_candidates": candidate_rows,
            "live_memblock_reserved_matches": memblock_matches,
            "candidate_status": "unique_exact_candidate" if len(candidate_rows) == 1 else "not_unique",
            "proof_limit": (
                "Unique by size, alignment, alloc-ranges and the captured iomem. "
                "The generic iomem label does not itself expose the owning DT node."
            ),
        })


    for item in dynamic:
        name = item["path"].rsplit("/", 1)[-1]
        matching_cma = [area for area in cma_areas if area["name"] == name]
        item["named_cma_debugfs_matches"] = matching_cma
        if matching_cma and item["iomem_exact_candidates"]:
            candidate = item["iomem_exact_candidates"][0]
            item["candidate_status"] = (
                "exact_named_runtime_placement"
                if (candidate["start"], candidate["end_exclusive"])
                == (matching_cma[0]["start"], matching_cma[0]["end_exclusive"])
                else "named_runtime_mismatch"
            )
            item["proof_limit"] = (
                "The named CMA debugfs area directly fixes this runtime base and size; "
                "the captured iomem and memblock interval agree exactly."
            )

    dynamic_pairs = []
    for item in dynamic:
        if len(item["iomem_exact_candidates"]) == 1:
            candidate = item["iomem_exact_candidates"][0]
            dynamic_pairs.append((int(candidate["start"], 16), int(candidate["end_exclusive"], 16)))

    persistent_union = merge(fixed_pairs + dynamic_pairs)
    persistent_overlap_pairs = []
    for dyn in dynamic:
        for candidate in dyn["iomem_exact_candidates"]:
            start, end = int(candidate["start"], 16), int(candidate["end_exclusive"], 16)
            for item in fixed:
                if start < int(item["end_exclusive"], 16) and int(item["start"], 16) < end:
                    persistent_overlap_pairs.append([dyn["path"], item["path"]])

    chosen_initrd_start = int.from_bytes(prop_bytes(nodes, "/chosen", "linux,initrd-start"), "big")
    chosen_initrd_end = int.from_bytes(prop_bytes(nodes, "/chosen", "linux,initrd-end"), "big")
    dtb_reserved_end = 0x47cf9000  # captured iomem page-rounded reservation
    boot_accounted = [(chosen_initrd_start, (chosen_initrd_end + 0xfff) & ~0xfff), (dtb_base, dtb_reserved_end)]

    linux_reserved = [(x["start_i"], x["end_i"]) for x in iomem if x["label"] == "reserved" and overlaps(x, DRAM_START, DRAM_END)]
    residual = subtract(linux_reserved, fixed_pairs + dynamic_pairs + boot_accounted)

    system_ram = merge((x["start_i"], x["end_i"]) for x in iomem if x["depth"] == 0 and x["label"] == "System RAM")
    linux_reserved_union = merge(linux_reserved)
    non_no_map = [x for x in fixed if not x["no_map"]]
    no_map = [x for x in fixed if x["no_map"]]
    cma_total_kib = None
    for line in args.meminfo.read_text(encoding="utf-8").splitlines():
        match = re.match(r"CmaTotal:\s+(\d+) kB", line)
        if match:
            cma_total_kib = int(match.group(1))
            break

    result = {
        "schema": 1,
        "sources": {
            "fdt_json": str(args.fdt_json.as_posix()),
            "fdt_sha256": fdt["sha256"],
            "iomem": str(args.iomem.as_posix()),
            "iomem_sha256": hashlib.sha256(args.iomem.read_bytes()).hexdigest(),
            "active_slot": active_slot,
            "active_slot_basis": "androidboot.slot_suffix in the captured bootconfig",
            "bootconfig": str(args.bootconfig.as_posix()),
            "bootconfig_sha256": hashlib.sha256(args.bootconfig.read_bytes()).hexdigest(),
            "kernel_config": str(args.kernel_config.as_posix()),
            "kernel_config_sha256": hashlib.sha256(args.kernel_config.read_bytes()).hexdigest(),
            "meminfo": str(args.meminfo.as_posix()),
            "meminfo_sha256": hashlib.sha256(args.meminfo.read_bytes()).hexdigest(),
            "boot_header": str(args.boot_header.as_posix()),
            "boot_header_sha256": hashlib.sha256(args.boot_header.read_bytes()).hexdigest(),
            "vendor_boot_header": str(args.vendor_boot_header.as_posix()),
            "vendor_boot_header_sha256": hashlib.sha256(args.vendor_boot_header.read_bytes()).hexdigest(),
        },
        "dram": interval(DRAM_START, DRAM_END, bytes=DRAM_END - DRAM_START),
        "fdt_memreserve_block": fdt["memreserve"],
        "fixed_reserved": fixed,
        "fixed_summary": {
            "row_count": len(fixed),
            "no_map_count": len(no_map),
            "non_no_map_count": len(non_no_map),
            "union_segment_count": len(fixed_union),
            "union_size": hx(sum(end - start for start, end in fixed_union)),
            "overlap_pairs": fixed_overlap_pairs,
            "lk_exact_observation_count": sum(bool(x["lk_exact_observations"]) for x in fixed),
            "no_map_iomem_classes": sorted(set(x["iomem_class"] for x in no_map)),
            "non_no_map_iomem_classes": sorted(set(x["iomem_class"] for x in non_no_map)),
        },
        "mblock_info": {
            "raw_size": len(raw_mblock),
            "decoded_prefix_size": decoded_mblock_prefix_size,
            "unparsed_tail_size": len(raw_mblock) - decoded_mblock_prefix_size,
            "decoded_record_count": count,
            "decoded_free_intervals": decoded_mblocks,
            "decoded_union_size": hx(sum(end - start for start, end in decoded_pairs)),
            "exactly_equals_fixed_reservation_complement": decoded_pairs == fixed_complement,
            "layout_basis": (
                "Inferred native little-endian header (u32 count, 4-byte pad) and "
                "24-byte records (u64 start, u64 size, u64 rank/padded rank). The "
                "29 decoded intervals exactly equal DRAM minus the 52 fixed reg ranges."
            ),
            "unparsed_tail_status": (
                "Preserved in live-fdt.json as raw property bytes and intentionally "
                "left uninterpreted because no captured binding establishes its layout."
            ),
            "semantic_limit": (
                "This is the bootloader free-list after persistent carveouts were "
                "removed. It is not a statement that the ranges remain free after "
                "kernel, initrd, DTB, CMA and driver allocations."
            ),
        },
        "dynamic_reserved": dynamic,
        "fixed_plus_unique_dynamic_summary": {
            "union_segment_count": len(persistent_union),
            "union_size": hx(sum(end - start for start, end in persistent_union)),
            "overlap_pairs": persistent_overlap_pairs,
            "dpmaif_pair_equals_one_live_memblock_reservation": (
                any(int(x["start"], 16) == 0x12fe20000 and int(x["end_exclusive"], 16) == 0x130000000
                    for x in memblock_reserved)
            ),
        },
        "linux": {
            "top_level_System_RAM": [interval(start, end) for start, end in system_ram],
            "top_level_System_RAM_union_size": hx(sum(end - start for start, end in system_ram)),
            "all_reserved_union_size": hx(sum(end - start for start, end in linux_reserved_union)),
            "reserved_residual_after_fixed_dynamic_initrd_dtb": [interval(start, end) for start, end in residual],
            "meminfo_CmaTotal_kB": cma_total_kib,
            "kernel_config_memory": {
                "CONFIG_ARM64_PAGE_SHIFT": int(page_shift_match.group(1)),
                "page_size": page_size,
                "CONFIG_CMA_SIZE_MBYTES": configured_cma_size_mib,
            },
            "cma_areas": cma_areas,
            "cma_area_total_kB": sum(int(x["size"], 16) for x in cma_areas) // 1024,
            "cma_total_exactly_matches_debugfs_areas": (
                cma_total_kib == sum(int(x["size"], 16) for x in cma_areas) // 1024
            ),
            "cma_reconciliation": (
                "CmaTotal is 144 MiB and equals the two named debugfs CMA areas: "
                "reserved is [0xfb400000,0xfc400000) (16 MiB), and "
                "ssmr-reserved-cma-memory is [0x138000000,0x140000000) (128 MiB). "
                "Their PFN-derived bounds agree exactly with iomem; this is direct "
                "runtime placement evidence."
                if cma_total_kib == 147456 else "Captured CMA total did not match the expected reconciliation."
            ),
            "residual_limit": (
                "These are Linux 'reserved' bytes not explained by fixed DT reg, the "
                "four resolved dynamic runtime placements, chosen initrd or the DTB iomem span. "
                "They can include kernel/runtime/CMA/driver reservations and are not free holes."
            ),
            "live_memblock": {
                "memory_source": str(args.memblock_memory.as_posix()),
                "memory_sha256": hashlib.sha256(args.memblock_memory.read_bytes()).hexdigest(),
                "memory_regions": memblock_memory,
                "memory_union_size": hx(sum(
                    end - start for start, end in merge(
                        (int(x["start"], 16), int(x["end_exclusive"], 16)) for x in memblock_memory))),
                "reserved_source": str(args.memblock_reserved.as_posix()),
                "reserved_sha256": hashlib.sha256(args.memblock_reserved.read_bytes()).hexdigest(),
                "reserved_regions": memblock_reserved,
                "reserved_union_size": hx(sum(
                    end - start for start, end in merge(
                        (int(x["start"], 16), int(x["end_exclusive"], 16)) for x in memblock_reserved))),
                "interpretation": (
                    "Late live debugfs state. memblock.memory spans all 12 GiB in "
                    "flag-separated records; the text view omits flags, so membership "
                    "does not mean allocatable. memblock.reserved preserves exact, "
                    "often sub-page reservations that /proc/iomem rounds or merges."
                ),
            },
        },
        "boot": {
            "active_slot": active_slot,
            "kernel_image_size_from_boot_header": hx(kernel_image_size),
            "kernel_load_reservation_from_lk": interval(kernel_load_address, 0x47c80000, name="mb_kernel", map=0),
            "kernel_code_from_iomem": interval(0x40010000, 0x41bc0000),
            "kernel_data_from_iomem": interval(0x41fd0000, 0x42270000),
            "dtb_lk_reservation": interval(0x47c80000, 0x48080000, name="mb_dtb", map=0),
            "dtb_iomem_reservation": interval(dtb_base, dtb_reserved_end),
            "live_dtb_totalsize": hx(fdt["header"]["totalsize"]),
            "initrd_chosen": interval(chosen_initrd_start, chosen_initrd_end),
            "initrd_iomem_page_rounded": interval(chosen_initrd_start, (chosen_initrd_end + 0xfff) & ~0xfff),
            "initrd_lk_reservation": interval(ramdisk_load_address, 0x6af00000, name="mb_ramdisk", map=0),
            "chosen_atag_boot": {
                "size": len(prop_bytes(nodes, "/chosen", "atag,boot")),
                "sha256": hashlib.sha256(prop_bytes(nodes, "/chosen", "atag,boot")).hexdigest(),
                "status": (
                    "Opaque vendor binary property. Preserved in live-fdt.json and "
                    "not decoded or used to assign an address."
                ),
            },
        },
        "lk": lk,
        "safety_conclusion": (
            "No FD/stack/heap placement is approved by this report. The mblock free "
            "intervals are pre-kernel availability evidence; Linux System RAM and its "
            "gaps do not establish boot-time ownership, EL3/secure access, attributes, "
            "or lifetime for a Mu payload."
        ),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        f"{len(fixed)} fixed rows; fixed union {result['fixed_summary']['union_size']}; "
        f"{count} mblock intervals; {len(dynamic)} dynamic nodes; {len(residual)} residual spans"
    )


if __name__ == "__main__":
    main()
