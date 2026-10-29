#!/usr/bin/env python3
"""Add the BCD system-store marker to a new hive file.

Run this with WSL Ubuntu's python3, where python3-hivex is installed.  The
source is opened read/write by hivex because that is required for editing,
but commit is always directed to a distinct, previously nonexistent output
path.  The source hash is checked after the commit.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys

try:
    import hivex
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit(
        "python-hivex is required; run this with WSL Ubuntu python3"
    ) from exc


REG_DWORD = 4
SYSTEM_VALUE = (REG_DWORD, struct.pack("<I", 1))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def semantic_snapshot(path: Path) -> dict[str, dict[str, tuple[int, bytes]]]:
    """Return every key and value while excluding hive allocation metadata."""
    hive = hivex.Hivex(os.fspath(path), write=False)
    result: dict[str, dict[str, tuple[int, bytes]]] = {}

    def visit(node: int, parent: str = "") -> None:
        name = hive.node_name(node)
        current = name if not parent else f"{parent}\\{name}"
        result[current] = {
            hive.value_key(value): hive.value_value(value)
            for value in hive.node_values(node)
        }
        for child in hive.node_children(node):
            visit(child, current)

    visit(hive.root())
    return result


def patch_copy(source: Path, output: Path, expected_sha256: str | None = None) -> tuple[str, str]:
    source = source.resolve(strict=True)
    output = output.resolve(strict=False)
    if source == output:
        raise ValueError("source and output must be different paths")
    if output.exists():
        raise FileExistsError(f"refusing to overwrite existing output: {output}")
    if not output.parent.is_dir():
        raise FileNotFoundError(f"output directory does not exist: {output.parent}")

    source_hash = sha256(source)
    if expected_sha256 and source_hash.lower() != expected_sha256.lower():
        raise ValueError(
            f"source SHA-256 mismatch: expected {expected_sha256.lower()}, got {source_hash}"
        )

    before = semantic_snapshot(source)
    description_path = "System\\Description"
    if description_path not in before:
        raise ValueError(f"BCD hive has no {description_path} key")
    if "System" in before[description_path]:
        raise ValueError("Description\\System already exists; refusing an ambiguous rewrite")

    try:
        hive = hivex.Hivex(os.fspath(source), write=True)
        root = hive.root()
        if hive.node_name(root) != "System":
            raise ValueError(f"unexpected BCD root name: {hive.node_name(root)!r}")
        description = hive.node_get_child(root, "Description")
        if not description:
            raise ValueError("BCD hive has no Description key")
        hive.node_set_value(
            description,
            {"key": "System", "t": REG_DWORD, "value": SYSTEM_VALUE[1]},
        )
        hive.commit(os.fspath(output))
        del hive

        if sha256(source) != source_hash:
            raise RuntimeError("source hive changed during copy commit")

        expected = {key: values.copy() for key, values in before.items()}
        expected[description_path]["System"] = SYSTEM_VALUE
        after = semantic_snapshot(output)
        if after != expected:
            raise RuntimeError(
                "output semantic diff was not exactly ADD "
                "System\\Description\\System REG_DWORD 1"
            )
    except Exception:
        if output.exists():
            output.unlink()
        raise

    return source_hash, sha256(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="existing BCD hive")
    parser.add_argument("output", type=Path, help="new file; must not exist")
    parser.add_argument(
        "--expect-sha256",
        help="refuse the operation unless the source has this SHA-256",
    )
    args = parser.parse_args()

    try:
        source_hash, output_hash = patch_copy(
            args.source, args.output, args.expect_sha256
        )
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(f"source_sha256={source_hash}")
    print(f"output_sha256={output_hash}")
    print("semantic_diff=ADD System\\Description\\System REG_DWORD 1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
