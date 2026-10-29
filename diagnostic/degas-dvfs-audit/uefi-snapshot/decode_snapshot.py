"""Decode DVF1 UEFI journals offline. This program never accesses hardware."""
import argparse
import json
from pathlib import Path
import re

CSRAM_BASE, CSRAM_SIZE = 0x11BC00, 0x1400
BASES = (0x11BC10, 0x11BD30, 0x11BE50)
CPUS = ((0, 1, 2, 3), (4, 5, 6), (7,))
STAGES = {1: "DXE", 2: "UEFI menu", 3: "UEFI before OS loader", 4: "UEFI before ExitBootServices"}
STATUSES = {0: "complete", 1: "bad argument", 2: "qualification failed", 3: "read failed", 4: "bad stage"}
WORD_COUNT = 109


def address_for_slot(slot):
    if not 0 <= slot < WORD_COUNT:
        raise ValueError("invalid slot")
    if slot == 0:
        return CSRAM_BASE + 0x4CC
    d, word = divmod(slot - 1, 36)
    offset = word * 4 if word < 32 else (0x84, 0x88, 0x8C, 0x114)[word - 32]
    return BASES[d] + offset


def _fields(text):
    pairs = [word.split("=", 1) for word in text.split()]
    if any(len(p) != 2 for p in pairs) or len({p[0] for p in pairs}) != len(pairs):
        raise ValueError("invalid or duplicate fields")
    return dict(pairs)


def parse_journal(text):
    frames, frame = [], None
    for line in text.splitlines():
        start = line.find("DVF1 ")
        if start < 0:
            continue
        line = line[start:]
        match = re.fullmatch(r"DVF1 ([BMDLE]) (.*)", line)
        if not match:
            raise ValueError("malformed DVF1 line")
        kind, tail = match.groups()
        f = _fields(tail)
        if kind == "B":
            if frame is not None:
                raise ValueError("truncated frame before next begin")
            if set(f) != {"src", "stage", "seq", "status", "qual", "read", "failed"} or f["src"] != "UEFI":
                raise ValueError("this format is a UEFI observation only")
            frame = {"source": f["src"], "stage": int(f["stage"]), "sequence": int(f["seq"], 16),
                     "status": int(f["status"]), "qualified": int(f["qual"]),
                     "read": int(f["read"]), "failed_slot": int(f["failed"]),
                     "raw": [0] * WORD_COUNT, "valid": [False] * WORD_COUNT, "seen": set()}
            continue
        if frame is None:
            raise ValueError("record without frame begin")
        if kind == "E":
            if set(f) != {"seq"} or int(f["seq"], 16) != frame["sequence"]:
                raise ValueError("sequence mismatch")
            expected = {"M"} | {("D", d) for d in range(3)} | {("L", d, i) for d in range(3) for i in range(0, 32, 8)}
            if frame["seen"] != expected:
                raise ValueError("missing journal records")
            del frame["seen"]
            validate_frame(frame)
            frames.append(frame)
            frame = None
            continue
        if kind == "M":
            if set(f) != {"ok", "raw"} or f["ok"] not in ("0", "1"):
                raise ValueError("invalid mode record")
            key = "M"
            frame["valid"][0] = f["ok"] == "1"
            frame["raw"][0] = int(f["raw"], 16)
        elif kind == "D":
            if set(f) != {"d", "mask", "en", "req", "ready", "lat"}:
                raise ValueError("invalid domain record")
            d, mask = int(f["d"]), int(f["mask"], 16)
            if not 0 <= d < 3 or not 0 <= mask < 1 << 36:
                raise ValueError("invalid domain/mask")
            key = ("D", d)
            base = 1 + d * 36
            frame["valid"][base:base + 36] = [bool(mask & (1 << i)) for i in range(36)]
            frame["raw"][base + 32:base + 36] = [int(f[n], 16) for n in ("en", "req", "ready", "lat")]
        else:
            if set(f) != {"d", "i", "raw"}:
                raise ValueError("invalid LUT record")
            d, i = int(f["d"]), int(f["i"])
            words = f["raw"].split(",")
            if not 0 <= d < 3 or i not in range(0, 32, 8) or len(words) != 8:
                raise ValueError("invalid LUT position/length")
            key = ("L", d, i)
            base = 1 + d * 36 + i
            frame["raw"][base:base + 8] = [int(w, 16) for w in words]
        if key in frame["seen"]:
            raise ValueError("duplicate journal record")
        frame["seen"].add(key)
    if frame is not None:
        raise ValueError("truncated frame at end of journal")
    if not frames:
        raise ValueError("no complete DVF1 frames")
    return frames


def validate_frame(f):
    status, count, qual, failed = (f[k] for k in ("status", "read", "qualified", "failed_slot"))
    if status not in STATUSES or not 0 <= count <= WORD_COUNT or not 0 <= qual <= WORD_COUNT:
        raise ValueError("invalid capture status/count")
    if f["stage"] not in STAGES and status != 4:
        raise ValueError("invalid UEFI stage")
    if f["source"] != "UEFI" or not 0 <= f["sequence"] <= 0xFFFFFFFF:
        raise ValueError("invalid source/sequence")
    if len(f["raw"]) != WORD_COUNT or len(f["valid"]) != WORD_COUNT:
        raise ValueError("invalid word arrays")
    if any(not 0 <= n <= 0xFFFFFFFF for n in f["raw"]):
        raise ValueError("word outside DWORD range")
    if f["valid"] != [i < count for i in range(WORD_COUNT)]:
        raise ValueError("valid bitmap is not the successful read prefix")
    if any(v != 0 for v, valid in zip(f["raw"], f["valid"]) if not valid):
        raise ValueError("invalid words must remain zero")
    if status == 0 and (count, qual, failed) != (109, 109, 109):
        raise ValueError("inconsistent complete capture")
    if status == 2 and not (count == 0 and qual == failed and 0 <= failed < 109):
        raise ValueError("inconsistent qualification failure")
    if status == 3 and not (qual == 109 and count == failed and 0 <= failed < 109):
        raise ValueError("inconsistent read failure")
    if status in (1, 4) and (count, qual, failed) != (0, 0, 109):
        raise ValueError("inconsistent argument/stage failure")


def decode_frame(frame):
    validate_frame(frame)
    raw, valid = frame["raw"], frame["valid"]
    mode = raw[0] if valid[0] else None
    result = {"format": "DVF1", "source": "UEFI read", "stage": STAGES.get(frame["stage"], "invalid stage"),
              "sequence": frame["sequence"], "capture_status": STATUSES[frame["status"]],
              "successful_reads": frame["read"], "failed_address": None if frame["failed_slot"] == 109 else f"0x{address_for_slot(frame['failed_slot']):08x}",
              "global_mode_raw": mode, "source_driver_mode": None if mode is None else ("LUT index" if mode == 0 else "kHz request (FDVFS path not qualified)"),
              "physical_clock_measured": False, "windows_runtime_observation": False,
              "classification": "Boot-stage controller/shared-memory observation; not Android sysfs or live Windows telemetry.",
              "limits": ["Words are sampled sequentially, not atomically.",
                         "LUT frequencies are available OPP encodings, not current clocks or thermal ceilings.",
                         "Request/readiness/enable words do not measure clock edges.",
                         "Global mode alone does not prove that the alternative FDVFS request path is inactive."],
              "domains": []}
    for d, base in enumerate(BASES):
        s = 1 + d * 36
        words = raw[s:s + 36]
        good = valid[s:s + 36]
        lut = [{"index": i, "raw": f"0x{words[i]:08x}" if good[i] else None,
                "encoded_mhz": words[i] & 0xFFF if good[i] else None} for i in range(32)]
        opps, previous, lut_complete, stop = [], 0, True, None
        for i in range(32):
            if not good[i]:
                lut_complete, stop = False, "missing LUT word"
                break
            mhz = words[i] & 0xFFF
            if mhz == previous:
                stop = "first repeated frequency (vendor terminator)"
                break
            opps.append(mhz * 1000)
            previous = mhz
        if not opps or any(a <= b for a, b in zip(opps, opps[1:])) or 0 in opps:
            lut_complete = False
        request = words[33] if good[33] else None
        candidate = None
        request_state = "unavailable"
        if request is not None and mode is not None:
            # The exact installed driver's .get accepts an index <=31 even
            # when the global write-mode flag selects kHz. LK leaves this
            # mixed encoding at OS handoff; mode alone cannot decode readback.
            if request <= 31:
                request_state = "index out of qualified LUT"
                if lut_complete and request < len(opps):
                    candidate, request_state = opps[request], "LUT index request"
            else:
                request_state = "kHz request outside qualified LUT range"
                if lut_complete and opps[-1] <= request <= opps[0]:
                    candidate, request_state = request, "kHz request (not proof of accepted/actual clock)"
        ready = words[34] if good[34] else None
        latency = words[35] if good[35] else None
        result["domains"].append({"domain": d, "physical_cpus_from_DT": CPUS[d], "base": f"0x{base:08x}",
            "lut_raw_and_encoded_mhz": lut, "source_driver_opp_khz": opps,
            "lut_qualified_for_request_decode": lut_complete, "lut_termination": stop or "32-word limit",
            "enable_raw": words[32] if good[32] else None,
            "request_raw": request, "request_interpretation": request_state,
            "requested_candidate_khz": candidate, "requested_candidate_mhz": candidate / 1000 if candidate is not None else None,
            "readiness_raw": ready, "cpufreq_ready_bit0": None if ready is None else bool(ready & 1),
            "svs_ready_bit1": None if ready is None else bool(ready & 2),
            "latency_raw_us": latency, "reported_transition_latency_ns": latency * 1000 if latency else None})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("journal", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    report = [decode_frame(f) for f in parse_journal(args.journal.read_text(errors="replace"))]
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
