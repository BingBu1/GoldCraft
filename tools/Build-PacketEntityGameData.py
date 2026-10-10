"""Verify the exact copied engine and publish packet capability symbols."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parent.parent


def validate(path):
    config = json.loads((ROOT/"native/client/packet_entities_10210.json").read_text(encoding="utf-8"))
    identity = runpy.run_path(str(ROOT/"tools/Inspect-EngineIdentity.py"))["inspect"](path)
    if identity["sha256"] != config["sha256"] or identity["crc64xz"].lower() != config["crc64"]:
        raise ValueError("Unverified engine: refusing packet capability catalog")
    read = runpy.run_path(str(ROOT/"tools/Build-VisibleEntityGameData.py"))["engine_bytes"](path)
    for record in config["records"]:
        payload = record["payload"]
        if record["kind"] == "global":
            for key in ("gv_rva","gv_sig","gv_sig_va","gv_va","gv_inst_offset","gv_inst_disp","gv_inst_length"):
                if not isinstance(payload.get(key),str): raise ValueError("Incomplete packet global metadata")
            base = int(payload["gv_va"],16)-int(payload["gv_rva"],16)
            ref = int(payload["gv_sig_va"],16)-base+int(payload["gv_inst_offset"],16)+int(payload["gv_inst_disp"],16)
            if struct.unpack("<I",read(ref,4))[0] != int(payload["gv_va"],16):
                raise ValueError("Packet global operand no longer matches")
        elif record["kind"] == "function":
            read(int(payload["func_rva"],16),int(payload["func_size"],16))
    for check in config["verification"]:
        expected = bytes.fromhex(check["bytes"])
        if read(int(check["rva"],16),len(expected)) != expected:
            raise ValueError("Packet parser/transport instruction changed: "+check["rva"])
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine",type=Path,required=True)
    parser.add_argument("--output",type=Path,default=ROOT/"dist/gamedata/goldcraft-packet")
    args = parser.parse_args()
    engine,output = args.engine.resolve(strict=True),args.output.resolve()
    if not engine.is_relative_to(ROOT) or not output.is_relative_to(ROOT):
        raise ValueError("Input and output must remain in the workspace")
    config = validate(engine)
    version = "goldcraft-packet-hl-10210"
    snapshot = {"schemaVersion":5,"source":{"snapshotSchemaVersion":8,"analysisOutputContractVersion":3,"gameVersion":version},
                "binaries":{"engine":{"windows":{"crc64":config["crc64"]}}},"records":config["records"]}
    payload = (json.dumps(snapshot,indent=2)+"\n").encode()
    index = {"schemaVersion":4,"versions":[{"gameVersion":version,"url":version+".json",
             "sha256":hashlib.sha256(payload).hexdigest(),"size":len(payload),"snapshotSchemaVersion":8,"fileCount":len(config["records"])}]}
    output.mkdir(parents=True,exist_ok=True)
    (output/(version+".json")).write_bytes(payload)
    (output/"index.json").write_text(json.dumps(index,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"symbols":len(config["records"]),"verifiedInstructions":len(config["verification"]),
                      "capacity":config["capacity"],"messageCapacity":config["messageCapacity"],"sha256":config["sha256"]}))


if __name__ == "__main__": main()
