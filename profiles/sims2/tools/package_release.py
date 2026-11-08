"""Package the tracked public source tree for Linux, never local game/build files."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import io
ROOT=Path(__file__).resolve().parents[3]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--version",default="0.3.1");p.add_argument("--output",type=Path,default=ROOT/"out/releases");args=p.parse_args()
    subprocess.run(["python" if __import__("os").name=="nt" else "python3",str(ROOT/"tools/check_public_tree.py")],check=True)
    args.output.mkdir(parents=True,exist_ok=True)
    names=[n for n in subprocess.check_output(["git","ls-files","-z"],cwd=ROOT).decode().split("\0") if n]
    archive=args.output/f"Sims2PSP-linux-x64-setup-{args.version}.tar.gz"
    with tarfile.open(archive,"w:gz") as tar:
        for name in names:
            path=ROOT/name
            info=tar.gettarinfo(str(path),arcname="Sims2PSP-source/"+name);info.uid=info.gid=0;info.uname=info.gname="";info.mtime=0
            info.mode=0o755 if name in {"setup.sh","launch.sh"} else 0o644
            with path.open("rb") as stream:tar.addfile(info,stream)
        manifest=json.loads((ROOT/"profiles/sims2/launcher/recomplauncher.json").read_text())
        manifest["config"][0]["file"]=[{"path":"{config}/Sims2PSPRecomp/settings.ini"}]
        manifest["repo"]="tokenbroker/sims2-psp-recomp"
        manifest["start"]={"program":["launch.sh"],"args":["{game}"],"env":{"SIMS2_MEMSTICK":"{local}/Sims2PSPRecomp/memstick"}}
        manifest["prepare"]={"program":["setup.sh"],"args":["{game}"],"label":"Compile and verify from your USA disc (C++ tools required)","timeout":7200}
        manifest["exit_codes"]["1"]="Source setup or runtime failed. See the setup output and included Linux instructions."
        data=(json.dumps(manifest,indent=2)+"\n").encode()
        info=tarfile.TarInfo("Sims2PSP-source/recomplauncher.json");info.size=len(data);info.mode=0o644
        tar.addfile(info,io.BytesIO(data))
    digest=hashlib.sha256(archive.read_bytes()).hexdigest()
    (archive.with_suffix(archive.suffix+".sha256")).write_text(digest+"  "+archive.name+"\n")
    print(f"Source installer: {archive.name}; {len(names)} tracked files; SHA256 {digest}")
if __name__=="__main__":main()
