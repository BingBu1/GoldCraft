#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual frozen VGUI2Extension objects with GDI and SDK boundary fixtures."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time

ROOT=pathlib.Path(__file__).resolve().parents[2]

def identity(path):
    path=pathlib.Path(path).resolve();data=path.read_bytes()
    return dict(path=str(path),size=len(data),sha256=hashlib.sha256(data).hexdigest())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=pathlib.Path,required=True)
    parser.add_argument('--environment',type=pathlib.Path,help='Optional saved x86 compiler environment; otherwise use the current developer shell')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--fonttools',type=pathlib.Path)
    parser.add_argument('--old-cache',action='store_true',help='Link the existing original build and require its stale-cache counterexample')
    args=parser.parse_args();out=args.output.resolve();build=args.build.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise SystemExit('Output must be a new workspace build directory')
    out.mkdir(parents=True);(out/'source').mkdir();(out/'fixtures').mkdir();(out/'temp').mkdir()
    inputs=[]
    for source in pathlib.Path(__file__).parent.glob('*'):
        if source.is_file():
            target=out/'source'/source.name;shutil.copy2(source,target);inputs.append(identity(target))
    if args.fonttools:sys.path.insert(0,str(args.fonttools.resolve()))
    from fontTools.ttLib import TTFont
    for label,filename in [('a','arial.ttf'),('b','times.ttf'),('memory-a','arial.ttf'),('memory-b','times.ttf')]:
        source=pathlib.Path(os.environ['WINDIR'])/'Fonts'/filename;inputs.append(identity(source))
        font=TTFont(source)
        for record in font['name'].names:
            if record.nameID in (1,3,4,6,16):value='GoldCraft Memory Fixture' if label.startswith('memory') else 'GoldCraft Refresh Fixture'
            elif record.nameID in (2,17):value='Regular'
            else:continue
            if record.nameID==6:value=value.replace(' ','')
            record.string=value.encode(record.getEncoding(),errors='replace')
        font.save(out/'fixtures'/('font-'+label+'.ttf'))
    env=dict(os.environ)
    if args.environment:env.update(json.loads(args.environment.read_text('utf-8-sig')))
    env['TEMP']=env['TMP']=str(out/'temp')
    records=json.loads((build/'compile_commands.json').read_text())
    template=next(x for x in records if x['file'].lower().endswith('fontmanager.cpp'))
    command=template['command'].replace(template['file'],str(out/'source/main.cpp'))
    command=re.sub(r'/Fo[^ ]+',lambda _: '/Fo'+str(out/'main.obj'),command)
    command=re.sub(r'/Fd[^ ]+',lambda _: '/Fd'+str(out/'main.pdb'),command)
    command=command.replace(' -c ',' /UNDEBUG -c ')
    if args.old_cache:command=command.replace(' -c ',' /DOLD_CACHE -c ')
    steps=[]
    def run(name,argv,expected=0):
        started=time.monotonic();process=subprocess.run(argv,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
        log=out/(name+'.log');log.write_bytes(process.stdout);steps.append(dict(name=name,exit=process.returncode,seconds=time.monotonic()-started,argv=argv,log=identity(log)))
        (out/'steps.json').write_text(json.dumps(steps,indent=2),'utf-8');print(name,process.returncode,flush=True)
        if process.returncode!=expected:print(process.stdout.decode(errors='replace')[-10000:]);raise SystemExit(process.returncode or 1)
    run('compile',command)
    objects=sorted((build/'CMakeFiles/VGUI2Extension.dir').rglob('*.obj'))
    inputs += [identity(x) for x in objects]
    compiler=pathlib.Path(json.loads((ROOT/'.tools/clang-toolchain.json').read_text())['root'])/'bin'
    flags=['/nologo','/machine:x86','/subsystem:console','/debug','/opt:ref','/opt:icf','/threads:1','/out:'+str(out/'test.exe'),'/pdb:'+str(out/'test.pdb'),str(out/'main.obj')]
    flags += [str(x) for x in objects]
    flags += ['imm32.lib','kernel32.lib','user32.lib','gdi32.lib','winspool.lib','shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib']
    response=out/'link.rsp';response.write_text(subprocess.list2cmdline(flags),'utf-8')
    run('link',[str(compiler/'lld-link.exe'),'@'+str(response)])
    (out/'inputs.json').write_text(json.dumps(inputs,indent=2),'utf-8')
    run('test',[str(out/'test.exe'),str(out/'fixtures')],1 if args.old_cache else 0)
    if not args.old_cache:
        abi_command=command.replace(str(out/'source/main.cpp'),str(out/'source/abi.cpp')).replace(str(out/'main.obj'),str(out/'abi.obj')).replace(str(out/'main.pdb'),str(out/'abi.pdb'))
        run('abi-compile',abi_command)
        run('abi-link',[str(compiler/'lld-link.exe'),'/nologo','/machine:x86','/subsystem:console','/threads:1','/out:'+str(out/'abi.exe'),str(out/'abi.obj'),'kernel32.lib'])
        run('abi-test',[str(out/'abi.exe'),str(build/'VGUI2Extension.dll')])
    unchanged=all(identity(row['path'])==row for row in inputs)
    (out/'result.json').write_text(json.dumps(dict(success=unchanged,inputsUnchanged=unchanged,steps=steps,deployed=False,boundary='Real x86 GDI, SDK KeyValues, Scheme2, FontManager, FontTextureCache and ResourceRefresh; mock engine texture upload, filesystem and surface. No game process.'),indent=2),'utf-8')
    if not unchanged:raise SystemExit('Frozen inputs changed')

if __name__=='__main__':main()
