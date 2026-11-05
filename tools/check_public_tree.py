"""Check tracked publication files; never print matched secret values."""
import re
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=root).decode().split("\0")
forbidden = {".iso", ".cso", ".elf", ".bin", ".exe", ".dll", ".lib", ".obj", ".pdb", ".dmp", ".bmp", ".zip", ".7z"}
patterns = {
    "private key": r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
    "GitHub credential": r"(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})",
    "AWS credential": r"AKIA[A-Z0-9]{16}",
    "personal Windows path": r"[A-Za-z]:[\\/]Users[\\/](?!Public(?:[\\/]|$)|Default(?:[\\/]|$))[^\\/\s\"<>]+",
}
errors = []
for name in filter(None, paths):
    path = root / name
    parts = set(Path(name).parts)
    if path.suffix.lower() in forbidden or parts.intersection({"out", "generated", "SAVEDATA", "game", "captures", "progress", ".venv"}) or path.name.startswith((".env", "generated_unit_")):
        errors.append(f"{name}: private/generated payload")
        continue
    if path.stat().st_size > 5 * 1024 * 1024:
        errors.append(f"{name}: unexpected large file")
    data = path.read_text(encoding="utf-8", errors="replace")
    for number, line in enumerate(data.splitlines(), 1):
        for label, pattern in patterns.items():
            if re.search(pattern, line):
                errors.append(f"{name}:{number}: {label}")
if errors:
    raise SystemExit("\n".join(errors))
print(f"Publication checks passed for {len(list(filter(None, paths)))} tracked files")
