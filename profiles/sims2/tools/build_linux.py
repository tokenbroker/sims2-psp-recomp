"""Build a native Linux Vulkan runtime from your own supported USA or European ISO."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import shutil
import subprocess
from prepare import prepare
from launcher_manifest import europe_language_setting
from check_local_build import check_local_build
from doctor import check as check_prerequisites

ROOT=Path(__file__).resolve().parents[3]
PROFILE=ROOT/"profiles/sims2"
def run(*args):
    subprocess.run([str(a) for a in args],check=True)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso",type=Path)
    parser.add_argument("--workspace",type=Path,default=ROOT/"out/sims2-linux")
    parser.add_argument("--jobs",type=int,default=2)
    parser.add_argument("--generate-only",action="store_true")
    args=parser.parse_args()
    if not 1<=args.jobs<=16:parser.error("--jobs must be 1..16")
    iso=args.iso.resolve();identity=prepare(iso,False,verify_only=True)
    workspace=args.workspace.resolve()
    if workspace==ROOT or ROOT.is_relative_to(workspace) or (workspace.is_relative_to(ROOT) and not workspace.is_relative_to(ROOT/"out")):
        parser.error("Choose a workspace under out/ or outside the checkout")
    if workspace==ROOT/"out":parser.error("Choose a separate build workspace")
    prerequisites=check_prerequisites()
    if not prerequisites["ok"]:
        missing=[c["name"] for c in prerequisites["checks"] if not c["ok"] and c.get("required",True)]
        parser.error("Missing prerequisites: "+", ".join(missing)+"; see docs/LINUX.md")
    run("pkg-config","--exists","libavcodec","libavutil","freetype2","fontconfig")
    workspace.mkdir(parents=True,exist_ok=True)
    with (workspace/"setup.lock").open("a+b") as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise SystemExit("Another setup is using this workspace")
        framework=workspace/"framework";generated=workspace/"generated";game=workspace/"game";build=workspace/"build";install=workspace/"install"
        run("cmake","-S",ROOT,"-B",framework,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DPSPRECOMP_BUILD_TESTS=OFF")
        run("cmake","--build",framework,"--target","psp_recomp","--parallel",args.jobs)
        prepare(iso,False,game,report_dir=workspace/"analysis")
        run(framework/"psp_recomp",game/"PSP_GAME/SYSDIR/BOOT.BIN","--auto",generated,identity["load_base"],identity["unit_span_bytes"])
        if args.generate_only:return
        run("cmake","-S",ROOT,"-B",build,"-G","Ninja","-DCMAKE_BUILD_TYPE=Release","-DPSPRECOMP_PROFILE=sims2", "-DSIMS2_HOST_TESTS_ONLY=OFF", "-DSIMS2_TIMING_RESEARCH=OFF","-DPSPRECOMP_BUILD_TESTS=OFF","-DPSPRECOMP_BUILD_PROFILE_TESTS=OFF","-DSIMS2_GRAPHICS_BACKEND=VULKAN",f"-DSIMS2_REGION={'EUROPE' if identity['disc_id']=='ULES-00211' else 'USA'}",f"-DSIMS2_GENERATED_DIR={generated}")
        run("cmake","--build",build,"--target","sims2","--parallel",args.jobs)
        stage_install(iso,workspace,build,install,identity)
        check_local_build(iso,install,workspace/"validation")

def stage_install(iso,workspace,build,install,identity):
    run("cmake","--install",build,"--prefix",install,"--component","sims2")
    prepare(iso,False,install/"game",report_dir=workspace/"analysis")
    manifest=json.loads((PROFILE/"launcher/recomplauncher.json").read_text())
    manifest["config"][0]["file"]=[{"path":"{config}/Sims2PSPRecomp/"+("settings-europe.ini" if identity["disc_id"]=="ULES-00211" else "settings.ini")}]
    if identity["disc_id"]=="ULES-00211": manifest["settings"].append(europe_language_setting("main"))
    manifest.pop("prepare",None)
    manifest["start"]["program"]=["sims2"]
    manifest["start"]["env"]={"SIMS2_WINDOW":"1"}
    manifest["game"]["label"]=f"Your supported The Sims 2 PSP ISO ({identity['disc_id']}), native Linux Vulkan preview."
    manifest["saves"]=["{local}/Sims2PSPRecomp/memstick/PSP/SAVEDATA"]
    # {local} is launcher-managed on every platform; direct launch uses XDG.
    manifest["start"]["env"]["SIMS2_MEMSTICK"]="{local}/Sims2PSPRecomp/memstick"
    manifest["exit_codes"]["1"]="The game could not start or reached an unsupported feature. Run run.sh in a terminal to inspect its output."
    (install/"recomplauncher.json").write_text(json.dumps(manifest,indent=2)+"\n")
    # Relative to the install folder, including paths containing spaces.
    (install/"run.sh").write_text('#!/bin/sh\nset -eu\nbase=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nif [ "$#" -ne 1 ]; then echo "Usage: $0 /path/to/USA.iso" >&2; exit 2; fi\nexec "$base/sims2" "$base/game/PSP_GAME/SYSDIR/BOOT.BIN" "$1"\n')
    (install/"run.sh").chmod(0o755)
    for name in ("LICENSE","README.md"):shutil.copy2(ROOT/name,install/name)
    shutil.copy2(PROFILE/"THIRD_PARTY.md",install/"THIRD_PARTY.md")
    shutil.copytree(PROFILE/"third_party",install/"licenses",dirs_exist_ok=True,ignore=lambda directory,names:[n for n in names if n not in {"imgui","tiny_aes","SDL3","ffmpeg","bigmission","yakumo","LICENSE","LICENSE.txt","UNLICENSE","COPYING.LGPLv2.1"}])
    (install/"local-build.json").write_text(json.dumps({"platform":"linux","graphics":"vulkan","disc_id":identity["disc_id"]},indent=2)+"\n")
    print(f"Native Linux runtime ready: {install}\nRun ./run.sh /path/to/USA.iso; keep this game-derived folder private.")

if __name__=="__main__":main()
