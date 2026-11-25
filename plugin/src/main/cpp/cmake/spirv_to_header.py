#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write a compiled SPIR-V module as a C++ header holding a uint32_t array."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()

    data = Path(args.input).read_bytes()
    if len(data) == 0 or len(data) % 4 != 0:
        raise SystemExit(f"{args.input}: SPIR-V size must be a non-zero multiple of 4 bytes")
    words = struct.unpack(f"<{len(data) // 4}I", data)

    lines = [
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        f"static const uint32_t {args.name}[] = {{",
    ]
    for i in range(0, len(words), 8):
        lines.append("    " + ", ".join(f"0x{w:08x}u" for w in words[i:i + 8]) + ",")
    lines.append("};")
    lines.append(f"static const size_t {args.name}_size = sizeof({args.name});")

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
