"""Repeatable fresh-boot performance/timing checks; game inputs and raw logs stay local."""
import argparse
import csv
import hashlib
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import platform
import subprocess
import time


def percentile(values, fraction):
    values=sorted(values)
    if not values:return 0.0
    index=(len(values)-1)*fraction
    low=int(index);high=min(low+1,len(values)-1)
    return values[low]+(values[high]-values[low])*(index-low)


def summarize(rows,warmup):
    sample=rows[warmup:]
    if not sample:raise ValueError("No measured frames remain after warmup")
    times=[(float(r["work_us"])+float(r["present_us"]))/1000 for r in sample]
    wall_ms=sum(times)
    guest_us=sum(int(r["guest_delta_us"]) for r in sample)
    return {"measured_frames":len(sample),"frame_ms_p50":round(percentile(times,.50),3),
            "frame_ms_p95":round(percentile(times,.95),3),"frame_ms_p99":round(percentile(times,.99),3),
            "request_fps":round(len(times)*1000/wall_ms,3) if wall_ms else 0,
            "guest_seconds":round(guest_us/1e6,6),"measured_wall_seconds":round(wall_ms/1000,6),
            "guest_to_wall_ratio":round(guest_us/(wall_ms*1000),4) if wall_ms else 0,
            "frames_over_33ms":sum(t>33.333 for t in times)}


def resident_bytes(pid,handle=None):
    try:
        if os.name=="nt":
            import ctypes as c
            from ctypes import wintypes as w
            class Counters(c.Structure):
                _fields_=[("cb",w.DWORD),("PageFaultCount",w.DWORD)]+[(name,c.c_size_t) for name in ["PeakWorkingSetSize","WorkingSetSize","QuotaPeakPagedPoolUsage","QuotaPagedPoolUsage","QuotaPeakNonPagedPoolUsage","QuotaNonPagedPoolUsage","PagefileUsage","PeakPagefileUsage"]]
            data=Counters();data.cb=c.sizeof(data)
            if c.windll.psapi.GetProcessMemoryInfo(w.HANDLE(handle),c.byref(data),data.cb):return data.PeakWorkingSetSize
        else:
            for line in Path(f"/proc/{pid}/status").read_text().splitlines():
                if line.startswith("VmHWM:"):return int(line.split()[1])*1024
    except (OSError,ValueError):pass
    return 0


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("iso",type=Path);p.add_argument("--probe",type=Path,required=True);p.add_argument("--boot",type=Path,required=True)
    p.add_argument("--output",type=Path,default=Path("out/benchmark"));p.add_argument("--cases",default="1:30,1:60,4:60")
    p.add_argument("--frames",type=int,default=900);p.add_argument("--warmup",type=int,default=600)
    p.add_argument("--hardware-label",default="Unspecified hardware");p.add_argument("--timeout",type=int,default=300)
    p.add_argument("--experimental-render-hz",type=int,default=0,help="Requires an explicitly compiled developer research build")
    p.add_argument("--visible",action="store_true",help="Present an actual window (or a virtual display in CI)")
    p.add_argument("--unpaced",action="store_true",help="Measure throughput instead of real-time behavior")
    args=p.parse_args()
    if args.frames<30 or args.frames%30 or not 0<=args.warmup<args.frames:p.error("frames must be a multiple of 30, warmup must be smaller")
    if args.visible and args.unpaced:p.error("visible and unpaced modes are separate experiments")
    iso=args.iso.resolve();probe=args.probe.resolve();boot=args.boot.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    root=Path(__file__).resolve().parents[3]
    def git_value(*command):
        r=subprocess.run(["git",*command],cwd=root,capture_output=True,text=True);return r.stdout.strip() if r.returncode==0 else "unavailable"
    metadata={"runtime_sha256":hashlib.sha256(probe.read_bytes()).hexdigest(),"source_revision":git_value("rev-parse","HEAD"),"source_dirty":bool(git_value("status","--porcelain")),"recorded_utc":datetime.now(timezone.utc).isoformat()}
    report={"format":1,**metadata,"scenario":"fresh boot, isolated empty save, no input","hardware":args.hardware_label,
            "platform":platform.system(),"architecture":platform.machine(),"paced":not args.unpaced,"presentation_mode":"window" if args.visible else "hidden/offscreen","experimental_render_hz":args.experimental_render_hz,"warmup_frames":args.warmup,"cases":[]}
    for case in args.cases.split(","):
        scale,cap=map(int,case.split(":"))
        if scale not in range(1,9) or not 0<=cap<=1000:p.error("cases are scale:cap, scale 1..8, cap 0..1000")
        folder=out/f"{scale}x-cap{cap}";folder.mkdir(exist_ok=True)
        # Refuse reuse of a populated memory stick, keeping every run comparable.
        memstick=folder/"memstick"
        if memstick.exists() and any(memstick.rglob("*")):raise SystemExit(f"Use a fresh output directory: {folder.name}")
        settings=folder/"settings.ini";settings.write_text(f"width=1920\nheight=1080\nrender_scale={scale}\nfps_cap={cap}\nvsync=0\nshow_performance=0\n")
        env={k:v for k,v in os.environ.items() if not k.startswith(("SIMS2_","PSPRECOMP_"))}
        env.update(SIMS2_WINDOW="1",SIMS2_WINDOW_HIDDEN="1",SIMS2_MUTE="1",SIMS2_SETTINGS_PATH=str(settings),SIMS2_MEMSTICK=str(memstick),SIMS2_STOP_AFTER_FRAMES=str(args.frames),SIMS2_FRAME_CSV=str(folder/"frames.csv"),SIMS2_STATE_JSON=str(folder/"state.json"),SIMS2_TIMING_TRACE=str(folder/"timing.json"),SIMS2_PROFILE="1",PSPRECOMP_HLE_REGISTRY=str(folder/"registered-hle.csv"))
        if args.experimental_render_hz:env["SIMS2_EXPERIMENTAL_RENDER_HZ"]=str(args.experimental_render_hz)
        if args.visible:env.pop("SIMS2_WINDOW_HIDDEN",None)
        if args.unpaced:env.pop("SIMS2_WINDOW");env["SIMS2_CAPTURE_LAST_BMP"]=str(folder/"last.bmp")
        started=time.monotonic();peak=0
        with (folder/"runtime.log").open("w",encoding="utf-8") as log:
            with subprocess.Popen([str(probe),str(boot),str(iso),"--max-dispatches","unlimited"],env=env,stdout=log,stderr=subprocess.STDOUT) as child:
                while child.poll() is None:
                    peak=max(peak,resident_bytes(child.pid,getattr(child,"_handle",None)))
                    if time.monotonic()-started>args.timeout:child.kill();raise SystemExit(f"Benchmark timed out: {folder.name}")
                    time.sleep(.05)
                if child.returncode!=3:raise SystemExit(f"Runtime failed with {child.returncode}; inspect private log in {folder.name}")
        elapsed=time.monotonic()-started
        with (folder/"frames.csv").open() as f:rows=list(csv.DictReader(f))
        state=json.loads((folder/"state.json").read_text())
        if state["frames"]!=args.frames:raise SystemExit("Runtime did not reach requested frames")
        adapter=next((line.split("adapter:",1)[1].strip() for line in (folder/"runtime.log").read_text(encoding="utf-8",errors="replace").splitlines() if line.startswith("[graphics]") and "adapter:" in line),"unreported")
        result={"adapter":adapter,"scale":scale,"cap":cap,"output":[1920,1080],"total_elapsed_seconds":round(elapsed,3),"peak_working_set_mib":round(peak/1024**2,2),**state,**summarize(rows,args.warmup)}
        report["cases"].append(result)
        (out/"summary.json").write_text(json.dumps(report,indent=2)+"\n")
        print(json.dumps(result),flush=True)
    print("Share only the reviewed aggregate summary.json; raw logs, paths, captures and saves remain local.")
if __name__=="__main__":main()
