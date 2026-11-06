"""Opt-in Windows integration test. Closes only this test's hidden game window."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
p=argparse.ArgumentParser(description=__doc__)
for key in ["iso","boot","probe"]:p.add_argument("--"+key,type=Path,required=True)
args=p.parse_args()
if os.name!="nt":raise SystemExit("Windows-only integration test")
user=c.windll.user32
callback_type=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
with tempfile.TemporaryDirectory(prefix="sims2-exit-") as folder:
 root=Path(folder);env={k:v for k,v in os.environ.items() if not k.startswith(("SIMS2_","PSPRECOMP_"))}
 env.update(SIMS2_WINDOW="1",SIMS2_WINDOW_HIDDEN="1",SIMS2_MUTE="1",SIMS2_MEMSTICK=str(root/"memstick"),SIMS2_SETTINGS_PATH=str(root/"settings.ini"),SIMS2_STATE_JSON=str(root/"state.json"))
 (root/"settings.ini").write_text("render_scale=1\n")
 with (root/"runtime.log").open("w") as log:
  child=subprocess.Popen([str(args.probe.resolve()),str(args.boot.resolve()),str(args.iso.resolve()),"--max-dispatches","unlimited"],env=env,stdout=log,stderr=subprocess.STDOUT)
  try:
   end=time.monotonic()+60
   while time.monotonic()<end and child.poll() is None:
    try:
     if json.loads((root/"state.json").read_text())["frames"]>=60:break
    except (OSError,ValueError):pass
    time.sleep(.05)
   else:raise AssertionError("Hidden runtime did not reach 60 frames")
   matched=[]
   @callback_type
   def visit(hwnd,param):
    pid=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(pid))
    if pid.value==child.pid:matched.append(hwnd)
    return True
   user.EnumWindows(visit,0)
   assert matched,"Owned test window was not found"
   for hwnd in matched:user.PostMessageW(hwnd,0x0010,0,0)
   code=child.wait(timeout=15)
   assert code==0,f"Normal window close returned {code}"
   print("Owned hidden-window close exits successfully without a diagnostic error")
  finally:
   if child.poll() is None:child.kill();child.wait()
