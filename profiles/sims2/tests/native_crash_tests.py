"""A real isolated native fault must leave an actionable, path-free report."""
import subprocess
import sys
result = subprocess.run([sys.argv[1]], capture_output=True, timeout=15)
text = result.stderr.decode("utf-8", errors="replace")
assert result.returncode != 0, "Crash probe did not fault"
assert "[native-crash] code=0xc0000005" in text, text
assert "module=sims2_native_crash_probe.exe offset=0x" in text, text
assert "access=write address=0x10" in text, text
assert "\\" not in text and "/" not in text, "Report contains a path"
print("Native fault recorded with module and offset, without module paths")
