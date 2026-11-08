"""Embed our compiled host shaders, never game-derived shaders."""
from pathlib import Path
import struct
import sys


def embed(out, vertex, fragment):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    text = "#pragma once\n#include <cstdint>\nnamespace sims2::gpu::shaders {\n"
    for name, path in zip(("vertex", "fragment"), (vertex, fragment)):
        data = Path(path).read_bytes()
        words = struct.unpack("<" + "I" * (len(data) // 4), data)
        text += "inline constexpr std::uint32_t " + name + "[]={" + ",".join(hex(x) for x in words) + "};\n"
    out.write_text(text + "}\n")


if __name__ == "__main__":
    embed(*sys.argv[1:])
