#!/usr/bin/env python3
"""WSL/hivex: copy a PE SYSTEM hive, changing only SAN and automount policy.

Usage: SOURCE OUTPUT, or --verify SYSTEM. Never loads a host registry hive.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

import hivex


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(hive):
    result = {}

    def visit(node, path):
        result[path] = {hive.value_key(v): hive.value_value(v)
                        for v in hive.node_values(node)}
        for child in hive.node_children(node):
            visit(child, path + ("\\" if path else "") + hive.node_name(child))

    visit(hive.root(), "")
    return result


def node(hive, path, create=False):
    current = hive.root()
    for part in path.split("\\"):
        child = hive.node_get_child(current, part)
        if not child and create:
            child = hive.node_add_child(current, part)
        if not child:
            raise ValueError(f"missing key: {path}")
        current = child
    return current


def policies(hive):
    sets = sorted(hive.node_name(n) for n in hive.node_children(hive.root())
                  if re.fullmatch(r"ControlSet[0-9]{3}", hive.node_name(n)))
    if not sets:
        raise ValueError("no offline ControlSet keys")
    return [{"path": cs + path, "name": name, "value": value}
            for cs in sets for path, name, value in (
                (r"\Services\partmgr\Parameters", "SanPolicy", 3),
                (r"\Services\mountmgr", "NoAutoMount", 1))]


def check(hive):
    changes = policies(hive)
    for change in changes:
        value = hive.node_get_value(node(hive, change["path"]), change["name"])
        if not value or hive.value_value(value) != (4, struct.pack("<I", change["value"])):
            raise ValueError(f"policy readback failed: {change}")
    return changes


def require_clean(path):
    data = path.read_bytes()
    if data[:4] != b"regf" or data[4:8] != data[8:12]:
        raise ValueError(f"not a clean registry hive: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    require_clean(args.source)
    source_hash = digest(args.source)
    if args.verify:
        if args.output:
            parser.error("--verify does not accept an output")
        changes = check(hivex.Hivex(str(args.source)))
        print(json.dumps({"sha256": source_hash, "readback": "passed", "changes": changes}))
        return
    if not args.output or args.output.resolve() == args.source.resolve():
        parser.error("a separate output file is required")
    if args.output.exists():
        raise FileExistsError(args.output)
    hive = hivex.Hivex(str(args.source), write=True)
    before = snapshot(hive)
    expected = {path: dict(values) for path, values in before.items()}
    changes = policies(hive)
    for change in changes:
        current = ""
        for part in change["path"].split("\\"):
            current += ("\\" if current else "") + part
            expected.setdefault(current, {})
        raw = struct.pack("<I", change["value"])
        expected[change["path"]][change["name"]] = (4, raw)
        hive.node_set_value(node(hive, change["path"], True),
                            {"key": change["name"], "t": 4, "value": raw})
    hive.commit(str(args.output))
    require_clean(args.output)
    verify = hivex.Hivex(str(args.output))
    check(verify)
    if snapshot(verify) != expected:
        raise AssertionError("unexpected registry semantic change")
    if digest(args.source) != source_hash:
        raise AssertionError("source hive changed")
    report = {"source_sha256": source_hash, "output_sha256": digest(args.output),
              "changes": changes, "readback": "passed",
              "semantic_diff": "only the listed policy values and missing parent keys",
              "source_unchanged": True, "host_registry_modified": False,
              "scope": "SAN3 and automount disabled; not a hardware write blocker"}
    Path(str(args.output) + ".json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
