from pathlib import Path
import importlib.util
spec=importlib.util.spec_from_file_location("benchmark",Path(__file__).parents[1]/"tools/benchmark.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
rows=[{"work_us":16000,"present_us":0,"guest_delta_us":16000} for _ in range(3)]+[{"work_us":80000,"present_us":0,"guest_delta_us":16000}]
r=module.summarize(rows,0)
assert r["frames_over_33ms"]==1 and r["frame_ms_p50"]==16
assert 16<r["frame_ms_p95"]<80 and r["guest_to_wall_ratio"]<1
assert module.summarize(rows,3)["measured_frames"]==1
try:module.summarize(rows,4)
except ValueError:pass
else:raise AssertionError("Empty benchmark sample accepted")
print("Benchmark statistics detect stalls and clock drift")
