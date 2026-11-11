#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the actual HUD exporter and real slot3/HUD consumer; retain a narrow receipt."""
import argparse,hashlib,json,os,re,shlex,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def record(path):
    path=Path(path).resolve();data=path.read_bytes()
    return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def read(path):return json.loads(Path(path).read_text('utf-8-sig'))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('compile-commands','environment','source','stock-source','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--expect-failure',action='store_true')
    parser.add_argument('--reuse-build',type=Path)
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh workspace build directory')
    stock=args.stock_source.resolve()
    if not stock.is_relative_to(ROOT/'sandbox'):raise ValueError('Stock source must be isolated inside workspace sandbox')
    out.mkdir(parents=True);env=os.environ|read(args.environment)
    jobs=[];compile_inputs={};exporter=ROOT/'tools/Prepare-CSOLHud.py'
    script_inputs=[record(Path(__file__)),record(exporter),record(ROOT/'tools/csol_resource_order.py')]
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    success=False;manifest=None;executable=None
    def execute(name,command,expected=0):
        start=time.monotonic();result=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
        log=out/(name+'.log');log.write_bytes(result.stdout)
        jobs.append(dict(name=name,command=command,exitCode=result.returncode,expectedExit=expected,
            seconds=time.monotonic()-start,log=record(log)))
        (out/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf8');print(name,result.returncode,flush=True)
        if result.returncode!=expected:
            print(result.stdout.decode('utf8','replace')[-5000:]);raise RuntimeError(name)
        return result.stdout
    try:
        exported=out/'resources'
        execute('export',[sys.executable,'-X','utf8','-B',str(exporter),'--source',str(args.source),
            '--stock-source',str(stock),'--output',str(exported)])
        manifest=read(out/'resources-manifest.json')
        if args.reuse_build:
            previous=read(args.reuse_build/'receipt.json')
            if not previous['inputsUnchanged'] or not previous['compileInputs']:raise ValueError('Unverified compile receipt')
            for entry in previous['compileInputs']:
                if record(entry['path'])!=entry:raise ValueError('Compiled input drift: '+entry['path'])
                compile_inputs[entry['path']]=entry
            executable=previous['executable']
            if executable is None:
                # A tool interruption can arrive after successful link but
                # before the EXE is sealed. Resume only that completed link.
                if previous['jobs'][-1]['name']!='link' or previous['jobs'][-1]['exitCode']:
                    raise ValueError('Previous consumer did not finish linking')
                executable=record(args.reuse_build/'knife.exe')
            if record(executable['path'])!=executable:raise ValueError('Reused consumer identity changed')
        else:
            command=next(item['command'] for item in read(args.compile_commands)
                if item['file'].replace('\\','/').endswith('/client/hud_style.cpp'))
            compiler=shlex.split(command,posix=False);flags=[];skip=False
            for value in compiler[1:]:
                if value=='--':break
                if skip:skip=False;continue
                if value=='/clang:-ivfsoverlay':skip=True;continue
                if value=='-c' or value.startswith(('/Fo','/Fd')) or ('\\build\\' in value and value.startswith('-I')):continue
                flags.append(value)
            flags.extend(['-I'+str(ROOT),'-I'+str(ROOT/'native/csol'),'-I'+str(ROOT/'native/csol/client'),'/showIncludes'])
            units=[('knife','tests/csol-hud-style/knife_selection.cpp'),('inventory','native/csol/client/weapons/weapon_inventory.cpp'),
                ('slots','native/csol/weapons/weapon_slots.cpp'),('style','native/csol/hud_style.cpp'),
                ('codec','native/csol/image_codec.cpp'),('font-memory','native/csol/client/resource_font_memory.cpp')]
            # Record project dependencies before compiling; /showIncludes also
            # records actual SDK/system headers before the final hash recheck.
            pending=[ROOT/path for _,path in units]
            while pending:
                path=pending.pop().resolve()
                if str(path) in compile_inputs:continue
                compile_inputs[str(path)]=record(path)
                for name in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"',path.read_bytes(),re.M):
                    found=next((p for p in (path.parent/name.decode(),ROOT/name.decode()) if p.is_file()),None)
                    if found:pending.append(found)
            for name,path in units:
                output=execute('compile-'+name,[compiler[0],*flags,'/Fo'+str(out/(name+'.obj')),
                    '/Fd'+str(out/(name+'.compile.pdb')),'-c','--',str(ROOT/path)])
                for included in re.findall(r'Note: including file:\s*([^\r\n]+)',output.decode('utf8','replace')):
                    path=Path(included.strip()).resolve();compile_inputs.setdefault(str(path),record(path))
            execute('link',[compiler[0],'/nologo','/clang:-fuse-ld=lld',*[str(out/(name+'.obj')) for name,_ in units],
                '/Fe:'+str(out/'knife.exe'),'/link','/machine:x86','/OPT:REF','user32.lib','gdi32.lib'])
            executable=record(out/'knife.exe')
        output=execute('consumer',[executable['path'],str(exported)],1 if args.expect_failure else 0)
        if args.expect_failure and b'original knife setter requires exported selected metadata' not in output:
            raise RuntimeError('Old-export failure did not reach the intended metadata regression')
        success=True
    finally:
        unchanged=all(record(entry['path'])==entry for entry in [*compile_inputs.values(),*script_inputs])
        if manifest:unchanged=unchanged and all(record(entry['path'])==entry for entry in manifest['inputs'])
        receipt=dict(success=success and unchanged,expectedFailure=args.expect_failure,inputsUnchanged=unchanged,
            revision=revision,compileInputs=list(compile_inputs.values()),scriptInputs=script_inputs,
            executable=executable,reuseBuild=str(args.reuse_build) if args.reuse_build else None,
            exportManifest=record(out/'resources-manifest.json') if manifest else None,jobs=jobs,
            boundary='Real exporter, catalog, codec, slot3 commands, inventory, HUD and legacy renderer; controlled engine/surface. No DLL, runtime, physical input, GPU or FPS.')
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf8')
    if not unchanged:raise RuntimeError('Inputs changed during run')
if __name__=='__main__':main()
