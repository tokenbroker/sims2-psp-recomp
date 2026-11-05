"""Embed our compiled host shaders, never game-derived shaders."""
from pathlib import Path
import struct
import sys
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
text="#pragma once\n#include <cstdint>\nnamespace sims2::gpu::shaders {\n"
for name,path in zip(("vertex","fragment"),sys.argv[2:]):
 data=Path(path).read_bytes()
 words=struct.unpack("<"+"I"*(len(data)//4),data)
 text+="inline constexpr std::uint32_t "+name+"[]={"+",".join(hex(x) for x in words)+"};\n"
out.write_text(text+"}\n")
