"""Inspect an owned PSP ISO for regional-port work; emit metadata, never game files."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from prepare import disc_files, read_at, SECTOR


def inspect(iso):
    with Path(iso).open("rb") as stream:
        files = disc_files(stream)
        sector, size = files["/UMD_DATA.BIN"]
        if size > 4096:
            raise ValueError("Unexpected disc identity size")
        disc_id = read_at(stream, sector * SECTOR, size).split(b"|", 1)[0].decode("ascii")
        sector, size = files["/PSP_GAME/SYSDIR/BOOT.BIN"]
        if size < 52 or size > 64 * 1024 * 1024:
            raise ValueError("Unexpected executable size")
        boot = read_at(stream, sector * SECTOR, size)
    if boot[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected an unencrypted 32-bit little-endian ELF; no decryption is performed")
    elf_type, machine, _, entry, phoff = struct.unpack_from("<HHIII", boot, 16)
    phsize, phcount = struct.unpack_from("<HH", boot, 42)
    if machine != 8 or phsize < 32 or phcount > 1024 or phoff + phsize * phcount > len(boot):
        raise ValueError("Invalid MIPS ELF program headers")
    segments = []
    for index in range(phcount):
        kind, offset, vaddr, _, file_bytes, memory_bytes, flags, _ = struct.unpack_from(
            "<IIIIIIII", boot, phoff + phsize * index)
        if kind == 1:
            if offset + file_bytes > len(boot) or file_bytes > memory_bytes:
                raise ValueError("Invalid ELF load segment")
            segments.append({"address": f"0x{vaddr:08X}", "offset": offset,
                             "file_bytes": file_bytes, "memory_bytes": memory_bytes, "flags": flags})
    return {"format": 1, "disc_id": disc_id, "boot_sha256": hashlib.sha256(boot).hexdigest(),
            "boot_bytes": len(boot), "elf_type": elf_type, "machine": machine,
            "entry": f"0x{entry:08X}", "load_segments": segments}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output", type=Path, help="Optional JSON metadata file; no ISO path or raw game data is included")
    args = parser.parse_args()
    text = json.dumps(inspect(args.iso), indent=2) + "\n"
    if args.output:
        if args.output.resolve() == args.iso.resolve() or (args.output.exists() and args.output.samefile(args.iso)):
            parser.error("Output must not overwrite the ISO")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
