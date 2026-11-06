"""Extract supported local disc inputs and run the PSPRecomp toolchain.

No decryption or third party Python packages required. Game files stay ignored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

FROZEN = getattr(sys, "frozen", False)
PROFILE = Path(sys._MEIPASS) if FROZEN else Path(__file__).resolve().parents[1]
ROOT = PROFILE.parents[1]
DEFAULT_GAME = Path(sys.executable).parent / "game" if FROZEN else PROFILE / "game"
SECTOR = 2048


def read_at(stream, offset, size):
    stream.seek(offset)
    result = stream.read(size)
    if len(result) != size:
        raise ValueError("Truncated ISO")
    return result


def disc_files(stream):
    length = stream.seek(0, 2)
    pvd = read_at(stream, 16 * SECTOR, SECTOR)
    if pvd[:7] != b"\x01CD001\x01":
        raise ValueError("Expected a plain ISO9660 PSP disc image")
    root = pvd[156:190]
    pending = [("", *struct.unpack_from("<I", root, 2),
                *struct.unpack_from("<I", root, 10))]
    seen = set()
    files = {}
    while pending:
        parent, sector, size = pending.pop()
        if (sector, size) in seen:
            raise ValueError("Repeated ISO directory")
        seen.add((sector, size))
        if size > 16 * 1024 * 1024 or sector * SECTOR + size > length:
            raise ValueError("Invalid directory extent")
        block = read_at(stream, sector * SECTOR, size)
        cursor = 0
        while cursor < len(block):
            count = block[cursor]
            if not count:
                cursor = (cursor // SECTOR + 1) * SECTOR
                continue
            record = block[cursor:cursor + count]
            cursor += count
            if count < 34 or len(record) != count or 33 + record[32] > count:
                raise ValueError("Invalid directory record")
            raw_name = record[33:33 + record[32]]
            if raw_name in (b"\x00", b"\x01"):
                continue
            name = raw_name.decode("ascii").split(";")[0]
            if name in ("", ".", "..") or any(c in name for c in "/\\:"):
                raise ValueError("Unsafe ISO filename")
            path = parent + "/" + name
            extent = struct.unpack_from("<I", record, 2)[0]
            size = struct.unpack_from("<I", record, 10)[0]
            if extent * SECTOR + size > length or record[25] & 0x80:
                raise ValueError("Invalid or multi-extent ISO file")
            if record[25] & 2:
                pending.append((path, extent, size))
            else:
                if path in files:
                    raise ValueError("Duplicate ISO file")
                files[path] = (extent, size)
    return files


def prepare(iso, assets, output_dir=None, verify_only=False, report_dir=None):
    output_dir = output_dir or DEFAULT_GAME
    identity = json.loads((PROFILE / "config/identity.json").read_text())
    with iso.open("rb") as stream:
        files = disc_files(stream)

        def get(path):
            extent, size = files[path]
            return read_at(stream, extent * SECTOR, size)

        disc_id = get("/UMD_DATA.BIN").split(b"|", 1)[0].decode("ascii")
        if disc_id != identity["disc_id"]:
            raise ValueError(f"Unsupported disc {disc_id}; expected {identity['disc_id']}")
        boot = get("/PSP_GAME/SYSDIR/BOOT.BIN")
        digest = hashlib.sha256(boot).hexdigest()
        if digest != identity["boot_sha256"] or boot[:4] != b"\x7fELF":
            raise ValueError("Unsupported BOOT.BIN contents")
        if verify_only:
            return identity
        selected = ["/UMD_DATA.BIN", "/PSP_GAME/PARAM.SFO", "/PSP_GAME/SYSDIR/BOOT.BIN"]
        if assets:
            selected.append("/PSP_GAME/USRDIR/pathcache.bin")
        for path in selected:
            destination = output_dir / path.lstrip("/")
            destination.parent.mkdir(parents=True, exist_ok=True)
            extent, remaining = files[path]
            stream.seek(extent * SECTOR)
            temporary = destination.with_name(destination.name + ".partial")
            with temporary.open("wb") as output:
                while remaining:
                    chunk = stream.read(min(remaining, 1024 * 1024))
                    if not chunk:
                        raise ValueError("Truncated ISO during extraction")
                    output.write(chunk)
                    remaining -= len(chunk)
            temporary.replace(destination)
        if not FROZEN:
            analysis = report_dir if report_dir is not None else PROFILE / "analysis"
            analysis.mkdir(parents=True, exist_ok=True)
            (analysis / "disc.json").write_text(json.dumps({
                "disc_id": disc_id, "boot_sha256": digest, "files": files
            }, indent=2) + "\n")
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="sims2-setup 0.2.2")
    parser.add_argument("iso", type=Path)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_GAME,
                        help="Directory receiving PSP_GAME and UMD_DATA.BIN")
    parser.add_argument("--verify-only", action="store_true", help="Validate without writing files")
    parser.add_argument("--assets", action="store_true", help="Also extract the 1 GB pathcache.bin")
    parser.add_argument("--analyze", action="store_true")
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--generated-dir", type=Path, default=PROFILE / "generated",
                        help="Local output directory for generated game code")
    parser.add_argument("--report-dir", type=Path, default=PROFILE / "analysis",
                        help="Local source-setup analysis report directory")
    parser.add_argument("--tools-dir", type=Path, default=ROOT / "out/framework/Release")
    args = parser.parse_args()
    if FROZEN and (args.analyze or args.generate):
        parser.error("Analysis and generation require the source checkout")
    if args.verify_only and (args.analyze or args.generate or args.assets):
        parser.error("--verify-only cannot be combined with --assets, --analyze or --generate")
    identity = prepare(args.iso, args.assets, args.output_dir, args.verify_only, args.report_dir)
    print(f"Verified {identity['disc_id']}: {identity['boot_sha256']}", flush=True)
    boot = args.output_dir / "PSP_GAME/SYSDIR/BOOT.BIN"
    if args.analyze:
        subprocess.run([str(args.tools_dir / "psp_analyze.exe"), str(boot),
                        str(args.report_dir / "sims2.json"), identity["load_base"]], check=True)
    if args.generate:
        subprocess.run([str(args.tools_dir / "psp_recomp.exe"), str(boot), "--auto",
                        str(args.generated_dir), identity["load_base"],
                        str(identity["unit_span_bytes"])], check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Setup failed: {error}", file=sys.stderr)
        sys.exit(1)
