"""Compare a local ELF import list to registrations recorded by the real host."""
import argparse
import csv
import json
from pathlib import Path

def inventory(imports,registered):
    result=[]
    for row in imports:
        key=(row["library"],int(row["nid"],16))
        result.append({"library":key[0],"nid":f"0x{key[1]:08X}","name":row.get("name", ""),"handler_registered":key in registered})
    return {"format":1,"import_count":len(result),"registered_count":sum(r["handler_registered"] for r in result),
            "meaning":"Registered does not mean complete or exercised; unsupported API modes can still stop.","imports":result}
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--imports",type=Path,required=True);p.add_argument("--registry",type=Path,required=True);p.add_argument("--output",type=Path,required=True);args=p.parse_args()
    with args.imports.open() as f:imports=list(csv.DictReader(f))
    with args.registry.open() as f:registered={(r[0],int(r[1],16)) for r in csv.reader(f) if len(r)==2}
    report=inventory(imports,registered);args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+"\n")
    print(f"{report['registered_count']}/{report['import_count']} imports have registered handlers; this is not a correctness percentage.")
