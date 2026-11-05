"""A direct JAL to an import must terminate code generation (no game data)."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def fixture():
    data = bytearray(0x400)

    def u16(offset, value):
        struct.pack_into("<H", data, offset, value)

    def u32(offset, value):
        struct.pack_into("<I", data, offset, value)

    base = 0x08804000
    data[:7] = b"\x7fELF\x01\x01\x01"
    u16(16, 2)
    u16(18, 8)
    u32(20, 1)
    u32(24, base)
    u32(28, 52)
    u32(32, 0x300)
    for offset, value in [(40, 52), (42, 32), (44, 1), (46, 40), (48, 3), (50, 2)]:
        u16(offset, value)
    for offset, value in [(52, 1), (56, 0x80), (60, base), (64, base),
                          (68, 0x180), (72, 0x180), (76, 7), (80, 4)]:
        u32(offset, value)
    stub = base + 0x170
    u32(0x80, 0x0C000000 | ((stub >> 2) & 0x03FFFFFF))
    u32(0x88, 0x03E00008)  # return after the import
    u32(0x1F0, 0x03E00008)  # import stub
    u32(0x1AC, base + 0x140)  # module.stub_top
    u32(0x1B0, base + 0x154)  # module.stub_end
    u32(0x1C0, base + 0x160)  # library name
    data[0x1C8] = 5
    u16(0x1CA, 1)
    u32(0x1CC, base + 0x15C)  # NID table
    u32(0x1D0, stub)
    u32(0x1DC, 0x109F50BC)
    data[0x1E0:0x1E0 + 8] = b"TestLib\0"
    names = b"\0.rodata.sceModuleInfo\0.shstrtab\0"
    data[0x280:0x280 + len(names)] = names
    for offset, value in [(0x328, 1), (0x32C, 1), (0x330, 2),
                          (0x334, base + 0x100), (0x338, 0x180), (0x33C, 52),
                          (0x350, 23), (0x354, 3), (0x360, 0x280), (0x364, len(names))]:
        u32(offset, value)
    return data


with tempfile.TemporaryDirectory(prefix="psprecomp_import_") as directory:
    root = Path(directory)
    elf = root / "import.elf"
    elf.write_bytes(fixture())
    subprocess.run([str(Path(sys.argv[1]).resolve()), str(elf), "--auto", str(root / "generated"),
                    "0x08804000", "64"], check=True, timeout=15)
    source = (root / "generated/generated_unit_0000.cpp").read_text()
    assert "ctx.pc = 0x08804170u;\n    return;" in source
    registry = (root / "generated/generated_registry.cpp").read_text()
    assert "TestLib" in registry, "Fixture did not exercise a recognized import"

    # An edge outside the discovered corpus must compile to runtime dispatch,
    # rather than an undefined recomp_unit_<bucket> C++ symbol.
    data = fixture()
    struct.pack_into("<I", data, 0x80, 0x08000000 | ((0x08805000 >> 2) & 0x03FFFFFF))
    elf.write_bytes(data)
    subprocess.run([str(Path(sys.argv[1]).resolve()), str(elf), "--auto",
                    str(root / "unmapped"), "0x08804000", "64"], check=True, timeout=15)
    source = (root / "unmapped/generated_unit_0000.cpp").read_text()
    assert "ctx.pc = 0x08805000u, rt.invoke_chained_call(ctx, &aot_mem)" in source
    assert "&recomp_unit_0064" not in source
