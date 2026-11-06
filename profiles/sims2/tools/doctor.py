"""Check source-installer prerequisites without reading a game or credentials."""
import argparse
import ctypes
import ctypes.util
import json
import os
import platform
import shutil
import subprocess
import sys

def check():
    result={"platform":platform.system(),"architecture":platform.machine(),"python":".".join(map(str,sys.version_info[:3])),"checks":[]}
    result["checks"].append({"name":"x86-64 host","ok":platform.machine().lower() in {"amd64","x86_64"}})
    required=["cmake"] if os.name=="nt" else ["cmake","ninja","pkg-config","c++"]
    for name in required:result["checks"].append({"name":name,"ok":shutil.which(name) is not None})
    if os.name!="nt":
        result["checks"].append({"name":"glslang shader compiler", "ok":any(shutil.which(n) for n in ("glslangValidator","glslang"))})
    if os.name=="nt":
        vswhere=os.path.join(os.environ.get("ProgramFiles(x86)",""),"Microsoft Visual Studio","Installer","vswhere.exe")
        present=False
        if os.path.isfile(vswhere):
            r=subprocess.run([vswhere,"-latest","-products","*","-requires","Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationVersion"],capture_output=True,text=True)
            present=r.returncode==0 and bool(r.stdout.strip())
        result["checks"].append({"name":"Visual Studio C++ tools","ok":present})
    elif shutil.which("pkg-config"):
        for name in ["libavcodec","libavutil","freetype2","fontconfig"]:
            r=subprocess.run(["pkg-config","--modversion",name],capture_output=True,text=True)
            result["checks"].append({"name":name,"ok":r.returncode==0,"version":r.stdout.strip() if r.returncode==0 else None})
        # A development package can exist while its build omits game codecs.
        library = ctypes.util.find_library("avcodec")
        try:
            codec = ctypes.CDLL(library) if library else None
            find = codec.avcodec_find_decoder_by_name if codec else None
            if find:
                find.argtypes = [ctypes.c_char_p]
                find.restype = ctypes.c_void_p
            for decoder in ("h264", "atrac3", "atrac3plus"):
                result["checks"].append({"name":"FFmpeg decoder "+decoder, "ok":bool(find and find(decoder.encode())), "required":decoder!="atrac3"})
        except (OSError, AttributeError):
            result["checks"].append({"name":"FFmpeg decoder library", "ok":False})
    result["ok"]=all(x["ok"] or not x.get("required",True) for x in result["checks"]) and sys.version_info>=(3,11)
    return result
if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--json",action="store_true");args=parser.parse_args()
    result=check()
    if args.json:print(json.dumps(result,indent=2))
    else:
        for c in result["checks"]:print(("OK   " if c["ok"] else "WARN " if not c.get("required",True) else "NEED ")+c["name"])
        print("See docs/BUILD.md or docs/LINUX.md for installation instructions.")
    raise SystemExit(0 if result["ok"] else 1)
