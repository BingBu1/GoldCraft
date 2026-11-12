#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build every Release|Win32 ReAPI project TU in-place, without deploy events."""
import sys
sys.dont_write_bytecode=True
import os,json,re,hashlib,subprocess,time,struct,xml.etree.ElementTree as ET,argparse
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);parser.add_argument('--compiler',type=Path,required=True);parser.add_argument('--resource-compiler',type=Path,required=True)
args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[2];MOD=ROOT/'external/ReAPI/reapi';CWD=MOD/'msvc';OUT=args.output.resolve();OUT.mkdir(parents=True,exist_ok=False)
for name in ('obj','temp','adapter/version','adapter/msvc'): (OUT/name).mkdir(parents=True,exist_ok=True)
def ident(p):p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def digest(p):return ident(p)['sha256']
proj=CWD/'reapi.vcxproj';tree=ET.parse(proj);ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'};sources=[];excluded=[]
for item in tree.findall('.//m:ClCompile[@Include]',ns):
 p=(CWD/item.attrib['Include'].replace('\\','/')).resolve()
 skip=any((x.text or '').strip().lower()=='true' and 'Release|Win32' in x.attrib.get('Condition','') for x in item.findall('m:ExcludedFromBuild',ns))
 (excluded if skip else sources).append(p)
assert len(sources)==33,(len(sources),sources)
release=next(x for x in tree.findall('m:ItemDefinitionGroup',ns) if 'Release|Win32' in x.attrib.get('Condition',''))
compileNode=release.find('m:ClCompile',ns);includes=[]
for text in compileNode.find('m:AdditionalIncludeDirectories',ns).text.split(';'):
 if '%(' in text:continue
 includes.append(Path(text.replace('$(ProjectDir)',str(CWD)+'\\').replace('\\','/')).resolve())
defines=[x for x in compileNode.find('m:PreprocessorDefinitions',ns).text.split(';') if '%(' not in x]
ENVFILE=ROOT/'build/csol-season2-trace-20261109/compiler-env.json';environment=json.loads(ENVFILE.read_text(encoding='utf8'));env=dict(os.environ)
for key,value in environment.items():
 for old in list(env):
  if old.casefold()==key.casefold():del env[old]
 env[key]=value
env['TEMP']=env['TMP']=str(OUT/'temp')
compiler=args.compiler.resolve(strict=True);linker=compiler.with_name('lld-link.exe');inspector=compiler.with_name('llvm-readobj.exe');rc=args.resource_compiler.resolve(strict=True)
# Freeze full project source/header/control universe and compiler/SDK candidates before commands.
inputs={}
def track(p):
 p=Path(p).resolve()
 if p.is_file():inputs[str(p).casefold()]=ident(p)
for p in MOD.rglob('*'):
 if p.is_file() and p.suffix.lower() in ('.cpp','.c','.h','.hpp','.inc','.inl','.def','.rc','.ico','.bat','.sh','.vcxproj','.props','.targets'):track(p)
for p in (ENVFILE,Path(__file__),compiler,linker,inspector,rc):track(p)
resourceDir=Path(subprocess.check_output([str(compiler),'-print-resource-dir'],text=True).strip())
sdkdirs=[Path(p) for key in ('INCLUDE','LIB') for p in environment.get(key,'').split(';') if p]+[resourceDir]
for folder in sdkdirs:
 if folder.is_dir():
  for p in folder.rglob('*'):
   if p.is_file() and (not p.suffix or p.suffix.lower() in ('.h','.hpp','.inl','.inc','.tcc','.lib')):track(p)
rev=subprocess.check_output(['git','rev-parse','HEAD'],cwd=MOD,text=True).strip();count=int(subprocess.check_output(['git','rev-list','--count','HEAD'],cwd=MOD,text=True));date=subprocess.check_output(['git','show','-s','--format=%cd','--date=format:%b %d %Y|%H:%M:%S', 'HEAD'],cwd=MOD,text=True).strip().split('|');dirty=bool(subprocess.check_output(['git','ls-files','-m'],cwd=MOD,text=True).strip());version=(MOD/'version/version.h').read_text();numbers={key:int(re.search(r'#define\s+VERSION_'+key+r'\s+(\d+)',version)[1]) for key in ('MAJOR','MINOR','MAINTENANCE')};v=f"{numbers['MAJOR']}.{numbers['MINOR']}.{numbers['MAINTENANCE']}.{count}";full=v+'-dev'+('+m' if dirty else '')
header='\n'.join(('#ifndef __APPVERSION_H__','#define __APPVERSION_H__',f'#define APP_VERSION "{full}"',f"#define APP_VERSION_C {numbers['MAJOR']},{numbers['MINOR']},{numbers['MAINTENANCE']},{count}",f'#define APP_VERSION_STRD "{v}"','#define APP_VERSION_FLAGS 0x0L',f'#define APP_COMMIT_DATE "{date[0]}"',f'#define APP_COMMIT_TIME "{date[1]}"',f'#define APP_COMMIT_SHA "{rev[:7]}"',f'#define APP_COMMIT_URL "https://github.com/rehlds/ReAPI/commit/{rev[:7]}"','#endif',''))
(OUT/'adapter/version/appversion.h').write_text(header);(OUT/'adapter/version/reapi_version.inc').write_text(f"#pragma once\n#define REAPI_VERSION {numbers['MAJOR']}{numbers['MINOR']}{count}\n#define REAPI_VERSION_MAJOR {numbers['MAJOR']}\n#define REAPI_VERSION_MINOR {numbers['MINOR']}\n")
for p in (OUT/'adapter/version').iterdir():track(p)
steps=[];result=dict(success=False,fullReapiDLL=True,sourceCopies=0,deployed=False,liveGameplay=False,head=rev,project=ident(proj),configuration='Release|Win32',sourceCount=len(sources),excluded=[str(p) for p in excluded],version=dict(value=full,commitCount=count,shallowCountBoundary='Exact git rev-list count used by matched upstream generator; checkout has count1, not a claimed upstream release build number.',source=ident(MOD/'version/version.h'),generator=ident(MOD/'version/appversion.bat'),generatedHeader=ident(OUT/'adapter/version/appversion.h')),contract=dict(originalToolset='v140_xp',originalISA='SSE2',originalFP='No explicit project setting; MSVC default precise retained explicitly',chosen='Authorized exact Clang x86/C++20/O3/ThinLTO/AVX2/fp:precise; original /MT, exception/defs/includes/exports/resource and 33 Release TUs retained',events='No project pre/post events invoked; no upstream generated files or automatic deployment'),steps=steps)
def save():result['inputsBefore']=list(inputs.values());(OUT/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
def job(name,command,dependency=False):
 rsp=OUT/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');log=OUT/(name+'.log');start=time.monotonic();save()
 with log.open('wb') as stream:
  proc=subprocess.Popen([command[0],'@'+str(rsp)] if name!='inspect' else command,cwd=CWD,env=env,stdout=stream,stderr=subprocess.STDOUT);result['activeProcess']=dict(pid=proc.pid,name=name);save();print(json.dumps(dict(start=name,pid=proc.pid)),flush=True);code=proc.wait(timeout=180)
 text=log.read_text(encoding='utf8',errors='replace');steps.append(dict(name=name,pid=proc.pid,command=command,exit=code,seconds=time.monotonic()-start,log=ident(log)));result.pop('activeProcess',None)
 if dependency:
  deps=[]
  for value in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
   p=(CWD/Path(value.strip())).resolve();key=str(p).casefold();assert key in inputs,('dependency not prefrozen',str(p));assert digest(p)==inputs[key]['sha256'];deps.append(str(p))
  steps[-1]['dependencies']=deps
 save();print(json.dumps(dict(end=name,exit=code)),flush=True)
 if code:raise RuntimeError(name+' failed; original log retained')
base=[str(compiler),'/c','/nologo','/std:c++20','/MT','/EHsc','/Gy','/GF','/GS-','/O2','/Ob2','/Oi','/Oy','/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin','--target=i686-pc-windows-msvc','/showIncludes','/Y-','/I'+str(OUT/'adapter/version')]+['/I'+str(p) for p in includes]+['/D'+d for d in defines]
try:
 objects=[]
 for i,source in enumerate(sources):
  obj=OUT/'obj'/f'{i:02}-{source.stem}.obj';job(f'{i:02}-{source.stem}',base+['/Fo'+str(obj),'/Fd'+str(OUT/'reapi.pdb'),str(source)],True);objects.append(obj)
 job('resource',[str(rc),'/nologo','/I'+str(OUT/'adapter/msvc'),'/I'+str(CWD),'/fo'+str(OUT/'reapi.res'),str(CWD/'reapi.rc')])
 dll=OUT/'reapi_amxx.dll';link=[str(linker),'/DLL','/MACHINE:X86','/SUBSYSTEM:WINDOWS','/OUT:'+str(dll),'/PDB:'+str(OUT/'reapi_amxx.pdb'),'/IMPLIB:'+str(OUT/'reapi_amxx.lib'),'/DEF:'+str(CWD/'reapi.def'),'/OPT:REF','/OPT:ICF','/DEBUG','/INCREMENTAL:NO','/threads:1','/opt:lldltojobs=1']+[str(p) for p in objects]+[str(OUT/'reapi.res'),'ws2_32.lib','kernel32.lib','user32.lib','advapi32.lib']
 job('link',link);job('inspect',[str(inspector),'--file-headers','--coff-exports','--coff-imports',str(dll)])
 b=dll.read_bytes();nt=struct.unpack_from('<I',b,60)[0];machine=struct.unpack_from('<H',b,nt+4)[0];magic=struct.unpack_from('<H',b,nt+24)[0];assert machine==0x14c and magic==0x10b
 txt=(OUT/'inspect.log').read_text();assert all(x in txt for x in ('AMXX_Attach','Meta_Attach','GiveFnptrsToDll','AMXX_Query'))
 result['artifact']=ident(dll);result['pe']=dict(machine='I386',machineValue=machine,optionalMagic='PE32',optionalMagicValue=magic,requiredExportsVerified=['AMXX_Attach','AMXX_Query','Meta_Attach','GiveFnptrsToDll']);result['success']=True
except Exception as ex:result['error']=str(ex);print(str(ex),flush=True)
finally:
 result['changedInputs']=[r['path'] for r in inputs.values() if not Path(r['path']).is_file() or digest(r['path'])!=r['sha256']];result['inputsUnchanged']=not result['changedInputs'];result['success']&=result['inputsUnchanged'];result['inputsAfter']=[ident(r['path']) for r in inputs.values() if Path(r['path']).is_file()];save()
print(json.dumps(dict(success=result['success'],steps=len(steps),sourceCount=len(sources),inputsUnchanged=result['inputsUnchanged'],artifact=result.get('artifact'))),flush=True);raise SystemExit(0 if result['success'] else 1)
