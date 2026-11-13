#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile current damage owners/fixture using exact production flags, no linking.

This only establishes compileability. Execute sdk.cpp with the shared sealed
runner after a new complete server build has compiled every changed header.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,time
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--template',type=Path,required=True)
p.add_argument('--environment',type=Path,required=True)
p.add_argument('--adapter-dir',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists()
for parent in (out,*out.parents):assert not parent.is_symlink() and not getattr(parent,'is_junction',lambda:False)()
out.mkdir(parents=True)
d=json.loads(a.template.read_text('utf-8-sig'));env=os.environ.copy();env.update(json.loads(a.environment.read_text('utf-8-sig')))
base=[x for x in d['compileCommand'] if not x.lower().startswith(('/yu','/fp','/fo','/fd')) and not x.lower().endswith('.cpp')]
base+=['/fp:precise','/showIncludes','/I'+str(a.adapter_dir.resolve()),'/I'+str(ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
files=['native/csol/server/scenario/core/human_kick_damage.cpp',
 'native/csol/server/scenario/monsters/scenario_monster_damage.cpp',
 'native/csol/server/scenario/core/scenario_death_owner.cpp',
 'native/csol/server/scenario/core/scenario_death_services.cpp',
 'native/csol/server/scenario/core/scenario_player_runtime.cpp',
 'native/csol/server/scenario/monsters/scenario_monster_ordinary.cpp',
 'native/csol/server/scenario/monsters/scenario_monster_position.cpp',
 'native/csol/server/scenario/monsters/scenario_monster_pool.cpp',
 'tests/csol-human-kick-damage/sdk.cpp']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
# Snapshot current source/header inputs before compiling, then check every
# actually consumed include and source. Unrelated concurrent files are ignored.
known={str(x.resolve()).lower():sha(x) for top in ['native','external/ReGameDLL_CS','external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include','tests'] for x in (ROOT/top).rglob('*') if x.is_file() and x.suffix.lower() in ('.h','.hpp','.inc','.cpp')}
for name in ['engine_edge_names.inc','engine_edge_defaults.inc']:
 x=(a.adapter_dir/name).resolve();known[str(x).lower()]=sha(x)
result=dict(success=False,template=str(a.template.resolve()),environment=str(a.environment.resolve()),gitRevision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),compiledOnly=True,linked=False,liveGameplay=False,sourceCopies=0,commands=[],inputs={})
for name in files:
 source=ROOT/name;cmd=base+[str(source),'/Fo'+str(out/(source.stem+'.obj'))]
 begin=time.monotonic();q=subprocess.run(cmd,cwd=ROOT/'external/ReGameDLL_CS/regamedll/msvc',env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 log=out/(source.stem+'.log');log.write_bytes(q.stdout)
 text=q.stdout.decode('utf-8',errors='replace')
 includes=[str(Path(x.strip()).resolve()).lower() for x in re.findall(r'Note: including file:\s*(.+)',text)]
 inputs={str(source.resolve()).lower(),*includes}
 changed=[x for x in inputs if x in known and sha(x)!=known[x]]
 unknown=[x for x in includes if x.startswith(str(ROOT).lower()) and x not in known]
 result['inputs'].update({x:known[x] for x in inputs if x in known})
 result['commands'].append(dict(source=name,command=cmd,exit=q.returncode,seconds=time.monotonic()-begin,log=str(log),changed=changed,unknown=unknown))
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(name,q.returncode,'changed',len(changed),'unknown',len(unknown),flush=True)
 if q.returncode or changed or unknown:
  print('\n'.join(x for x in text.splitlines() if 'error:' in x or 'warning:' in x));raise SystemExit(1)
result['changedAfter']=[x for x,v in result['inputs'].items() if sha(x)!=v]
result['success']=not result['changedAfter']
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
raise SystemExit(0 if result['success'] else 1)
