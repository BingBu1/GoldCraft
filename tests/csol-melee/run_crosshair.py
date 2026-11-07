# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual HUD gate and melee descriptor against SDK declarations."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def identity(p):
    p=Path(p);return dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--candidate',type=Path,required=True);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--build-base',type=Path,required=True);a=ap.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('cpp',ROOT/'tests/common/cpp_function.py');cpp=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpp)
    provenance=json.loads((a.build_base/'complete-object-provenance.json').read_text());unit=next(u for u in provenance['units']if u['source']['path'].replace('\\','/').endswith('/client/weapons.cpp'))
    flags=[v for v in unit['command'][1:]if v.startswith(('-I','-D','/D','--target='))]
    flags+=['/std:c++20','/MT','/EHsc','/O2','/utf-8','-I'+str(ROOT/'native/csol')]
    env=os.environ|json.loads((a.build_base/'environment.json').read_text())
    records=[]
    for name,source in [('new',a.candidate),('old',a.baseline)]:
        dest=out/name;dest.mkdir();raw=(source/'native/csol/client/weapons/weapons.cpp').read_bytes();melee=(source/'native/csol/client/weapons/melee.cpp').read_bytes()
        (dest/'actual_crosshair.inc').write_bytes(b'\n'.join(cpp.function(raw,sig)[2] for sig in [b'bool Custom()',b'bool MagazineActive()',b'void DrawCrosshair(int intermission)']))
        begin=melee.index(b'constexpr int ModuleIds[]');end=melee.index(b'const WeaponModuleRegistration Registration{Module};',begin)+len(b'const WeaponModuleRegistration Registration{Module};')
        (dest/'actual_melee_module.inc').write_bytes(melee[begin:end])
        fixture=dest/'test.cpp';fixture.write_bytes((ROOT/'tests/csol-melee/crosshair.cpp').read_bytes())
        sources=[fixture,ROOT/'native/csol/client/crosshair.cpp',ROOT/'native/csol/client/weapons/weapon_module.cpp']
        command=[unit['command'][0],*flags,*map(str,sources),'/Fe'+str(dest/'test.exe'),'/link','/MACHINE:X86']
        result=subprocess.run(command,cwd=dest,env=env,capture_output=True,timeout=120);(dest/'build.log').write_bytes(result.stdout+result.stderr)
        records.append(dict(name=name,command=command,compileExit=result.returncode,inputs=[identity(x)for x in sources]))
        if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode('utf8','replace')[-6000:])
        r=subprocess.run([dest/'test.exe'],capture_output=True,timeout=30);(dest/'test.log').write_bytes(r.stdout+r.stderr);records[-1].update(exitCode=r.returncode,output=(r.stdout+r.stderr).decode('utf8','replace'))
        print(name,records[-1]['output'])
    success=records[0]['exitCode']==0 and records[1]['exitCode']!=0
    (out/'result.json').write_text(json.dumps(dict(success=success,runs=records),indent=2)+'\n');assert success
if __name__=='__main__':main()
