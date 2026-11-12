#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import os,json,subprocess,sys,hashlib,re,argparse
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);parser.add_argument('--compiler',type=Path,required=True)
args=parser.parse_args();compiler=args.compiler.resolve(strict=True)
ROOT=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);(out/'temp').mkdir()
env=dict(os.environ)
for k,v in json.loads((ROOT/'build/csol-season2-trace-20261109/compiler-env.json').read_text()).items():
 for old in list(env):
  if old.casefold()==k.casefold():del env[old]
 env[k]=v
env['TEMP']=env['TMP']=str(out/'temp');module=ROOT/'external/ReAPI/reapi'
(out/'reapi_version.inc').write_text('#pragma once\n#include \"'+str(module/'version/version.h').replace('\\','/')+'\"\n#define REAPI_VERSION_MAJOR VERSION_MAJOR\n#define REAPI_VERSION_MINOR VERSION_MINOR\n')
includes=['src','src/mods','src/natives','common','version','include','include/metamod','include/cssdk/common','include/cssdk/dlls','include/cssdk/engine','include/cssdk/game_shared','include/cssdk/pm_shared','include/cssdk/public']
cmd=[str(compiler),'/c','/nologo','/std:c++20','/MT','/EHsc','/Gy','/O2','/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin','--target=i686-pc-windows-msvc','/showIncludes','/DWIN32','/DHAVE_STRONG_TYPEDEF','/DNDEBUG','/D_WINDOWS','/D_CRT_SECURE_NO_WARNINGS']+['/I'+str(out)]+['/I'+str(module/p) for p in includes]+['/Fo'+str(out/'reapi.obj'),str(ROOT/'tests/csol-null-attacker/reapi.cpp')]
steps=[];inputs={}
def identity(p):return dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def job(name,command):
 rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');r=subprocess.run(command if name=='behavior' else [command[0],'@'+str(rsp)],env=env,capture_output=True,timeout=240,cwd=module/'msvc');log=out/(name+'.log');log.write_bytes(r.stdout+r.stderr);text=log.read_text(encoding='utf8',errors='replace');steps.append(dict(name=name,command=command,exit=r.returncode))
 for p in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
  path=Path(p.strip()).resolve();inputs[str(path)]=identity(path)
 (out/'receipt.json').write_text(json.dumps(dict(success=False,steps=steps,inputs=list(inputs.values()),controlledBoundary='VM ExecuteForward and next-chain; actual five wrapper bodies and actual forwarding template compile from original source, no full AMXX VM'),indent=2)+'\n')
 print(name,r.returncode,flush=True)
 if r.returncode:print(text[-10000:],flush=True);raise SystemExit(1)
job('compile',cmd)
job('link',[str(compiler.with_name('lld-link.exe')),'/OUT:'+str(out/'reapi-null.exe'),'/SUBSYSTEM:CONSOLE','/MACHINE:X86','/threads:1','/opt:lldltojobs=1',str(out/'reapi.obj'),'kernel32.lib','user32.lib','advapi32.lib','ws2_32.lib'])
job('behavior',[str(out/'reapi-null.exe')]);r=json.loads((out/'receipt.json').read_text());r['success']=True;(out/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');print((out/'behavior.log').read_text())
