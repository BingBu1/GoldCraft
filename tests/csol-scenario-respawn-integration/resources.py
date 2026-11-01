"""Exercise real respawn resource contracts and the unchanged fixed-precache block.

The full map/BGM/character collector is not executed here. Its fixed-precache
block runs unchanged with real literal/path validation and a recording add
boundary. Actual common assets and delivered ZIP bytes are read, never copied.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import importlib.util
import json
import re
import sys
import zipfile
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
EXPECTED={'resource/Hud/ZBS/roundclear.tga','resource/Hud/ZBS/roundfail.tga',
          'sprites/zbs_level.spr','resource/Hud/Hud_string_bg.tga','resource/cso_chn.txt',
          'resource/Hud/ZBS/continue.tga','sound/zombi/TD_Heal.wav'}
EXPECTED.update('resource/Hud/ZBS/Number'+str(n)+'.tga' for n in range(10))
EXPECTED.update('sound/vox/'+n+'.wav' for n in ('One','Two','Three','Four','Five','Six','Seven','Eight','Nine','Ten'))

def identity(path):
    with path.open('rb') as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path.resolve()),bytes=path.stat().st_size,sha256=sha)

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec)
    sys.modules[name]=module;spec.loader.exec_module(module);return module

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--old-source',required=True,type=Path)
    parser.add_argument('--common',required=True,type=Path)
    parser.add_argument('--delivered-receipt',required=True,type=Path)
    parser.add_argument('--baseline-audit',required=True,type=Path)
    args=parser.parse_args();out=args.output.resolve();source=args.source_root.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace build output')
    out.mkdir(parents=True);sys.path.insert(0,str(source/'tools'))
    resource=load('respawn_resource_production',source/'tools/csol_scenario_resources.py')
    closure=load('respawn_closure_production',source/'tools/csol_server_table_closure.py')
    checks=[];evidence={};failures=[]
    def check(value,label):
        if not value:raise AssertionError(label)
        checks.append(label)
    def raises(callback,label):
        try:callback()
        except (ValueError,FileNotFoundError) as error:failures.append(dict(case=label,error=type(error).__name__));check(True,label)
        else:check(False,label)
    paths,info=resource.respawn_ui_contract(source)
    check(paths==EXPECTED and len(paths)==27,'production shared header yields all 27 original revival/retry assets')
    header=source/'native/csol/scenario_respawn_resources.hpp';original=header.read_text('utf-8-sig')
    check(info['headerSha256']==identity(header)['sha256'],'contract evidence binds actual header bytes')
    historical=resource.respawn_ui_contract(args.old_source.resolve())
    check(historical==(set(),None),'actual frozen old Session does not acquire new resource dependencies')

    # Extract the exact new block by AST source positions; no token replacement.
    script=source/'tools/csol_server_table_closure.py';text=script.read_text('utf-8-sig');tree=ast.parse(text)
    collect=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='collect')
    start=next(i for i,node in enumerate(collect.body) if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='owners' for t in node.targets))
    end=next(i for i,node in enumerate(collect.body[start:],start) if isinstance(node,ast.For) and isinstance(node.target,ast.Name) and node.target.id=='owner')
    selected=collect.body[start:end+1];block='\n'.join(text.splitlines()[selected[0].lineno-1:selected[-1].end_lineno])+'\n'
    # A function body shares the original indentation. Parameters replace only
    # the caller's surrounding map collector locals and recording add callback.
    code='def fixed(source,sources,add):\n'+block
    namespace=dict(closure.__dict__);exec(compile(code,str(script)+':fixed-precache','exec'),namespace)
    owners=ast.literal_eval(selected[0].value)
    (out/'fixed-precache.py.txt').write_text(code,encoding='utf-8')
    evidence['fixedBlock']=dict(script=identity(script),firstLine=selected[0].lineno,lastLine=selected[-1].end_lineno,
        sha256=hashlib.sha256(block.encode()).hexdigest(),actualStatements=len(selected))
    def fixture(name,include,header_text=original):
        root=out/name
        for owner in owners:
            path=root/'native/csol'/owner;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('// SPDX-License-Identifier: GPL-3.0-or-later\n',encoding='utf-8')
        session=root/'native/csol/server/scenario_session.cpp'
        session.write_text('// SPDX-License-Identifier: GPL-3.0-or-later\n'+('#include "../scenario_respawn_resources.hpp"\n' if include else ''),encoding='utf-8')
        if header_text is not None:(root/'native/csol/scenario_respawn_resources.hpp').write_text(header_text,encoding='utf-8')
        return root
    def fixed(root):
        recorded=[];sources={}
        namespace['fixed'](closure.SourceTree(root),sources,lambda name,*a,**kw:recorded.append(closure.relative(name)))
        return set(recorded),sources
    old=fixture('old',False)
    check(resource.respawn_ui_contract(old)==(set(),None) and fixed(old)[0]==set(),'old selected Session ignores unrelated new header')
    missing=fixture('missing',True,None)
    raises(lambda:resource.respawn_ui_contract(missing),'main resource contract rejects missing included header')
    raises(lambda:fixed(missing),'fixed precache rejects missing included header')
    current=fixture('current',True)
    check(resource.respawn_ui_contract(current)[0]==EXPECTED and fixed(current)[0]==EXPECTED,'both production collection paths match all 27 assets')
    names,sources=fixed(current)
    check(any(name.endswith('scenario_respawn_resources.hpp') for name in sources),'fixed collector records shared header identity')
    extended=fixture('extended',True,original+'\ninline constexpr char NewResource[]="resource/Hud/ZBS/future_test.tga";\n')
    check(resource.respawn_ui_contract(extended)[0]==EXPECTED|{'resource/Hud/ZBS/future_test.tga'} and
        fixed(extended)[0]==EXPECTED|{'resource/Hud/ZBS/future_test.tga'},'adding shared header asset updates both collectors automatically')
    empty=fixture('empty',True,'// SPDX-License-Identifier: GPL-3.0-or-later\n')
    raises(lambda:resource.respawn_ui_contract(empty),'empty resource header is rejected')
    raises(lambda:fixed(empty),'fixed collector also rejects an empty included resource header')
    invalid=fixture('invalid',True,original+'\ninline constexpr char Bad[]="resource/Hud/../outside.tga";\n')
    raises(lambda:resource.respawn_ui_contract(invalid),'main collector rejects traversal resource')
    raises(lambda:fixed(invalid),'fixed collector rejects traversal resource')
    invalid_sound=fixture('invalid-sound',True,original+'\ninline constexpr char BadSound[]="sound/vox/../../outside.wav";\n')
    raises(lambda:resource.respawn_ui_contract(invalid_sound),'main collector rejects traversal retry sound')
    raises(lambda:fixed(invalid_sound),'fixed collector rejects traversal retry sound')
    extended_sound=fixture('extended-sound',True,original+'\ninline constexpr char ExtraSound[]="sound/vox/roundclear.wav";\n')
    check(resource.respawn_ui_contract(extended_sound)[0]==EXPECTED|{'sound/vox/roundclear.wav'} and
        fixed(extended_sound)[0]==EXPECTED|{'sound/vox/roundclear.wav'},'new shared sound is collected by both production paths')

    # Execute unchanged main(), controlling only unrelated map/model contracts
    # and external executables. The revival contract, entries merge, common
    # file hashes and respawnUiClosure serialization remain real production.
    previous=out/'empty-previous.json'
    previous.write_text('{"files":[],"models":[]}',encoding='utf-8')
    command_calls=[]
    def command(_tool,*arguments):
        command_calls.append([str(item) for item in arguments])
        if arguments[0]=='--catalog':return {'effectiveTypes':[]}
        if arguments[0]=='--plan':return {'uniqueDependencyFiles':0}
        raise AssertionError('Unexpected unrelated model collector execution')
    def generator(selected,label):
        for relative in ('native/csol/server/scenario_monster_resources.h',
                         'native/csol/server/scenario_monster_skills.h',
                         'native/csol/scenario_monster_effects.hpp'):
            target=selected/relative
            if not target.exists():target.parent.mkdir(parents=True,exist_ok=True);target.write_text('// fixture\n',encoding='utf-8')
        target=out/('main-'+label)
        argv=['resources.py',str(args.common),str(script),str(script),str(previous),str(target),'--source-root',str(selected)]
        with patch.object(sys,'argv',argv), patch.multiple(resource,
            boss_contract=lambda *_:None,smoke_event_contract=lambda *_:None,
            dialogue_contract=lambda *_:(set(),None),elite_contract=lambda *_:set(),
            frozen_death_contract=lambda *_:(set(),[]),skill_contract=lambda *_:set(),command=command):
            resource.main()
        return json.loads((target/'resource-plan.json').read_text('utf-8'))
    main_plan=generator(current,'current')
    check({row['destination'] for row in main_plan['files']}==EXPECTED,'actual main merges all 27 revival resources')
    check(main_plan['respawnUiClosure']['resources']==sorted(EXPECTED),'actual main serializes the shared revival closure')
    check(all(row['sha256']==identity(args.common/row['destination'])['sha256'] for row in main_plan['files']),
          'actual main records the original common SHA without asset copies')
    old_plan=generator(old,'old')
    check(old_plan['files']==[] and 'respawnUiClosure' not in old_plan,'actual old main has no new revival resources or evidence')
    raises(lambda:generator(missing,'missing'),'actual main rejects missing included revival header before output')
    addition='sound/vox/roundclear.wav'
    extended_main=fixture('extended-main',True,original+'\ninline constexpr char NextResource[]="'+addition+'";\n')
    extended_plan=generator(extended_main,'extended')
    check({row['destination'] for row in extended_plan['files']}==EXPECTED|{addition} and
        extended_plan['respawnUiClosure']['resources']==sorted(EXPECTED|{addition}),'actual main automatically merges an additional existing header asset')
    evidence['mainBoundary']=dict(executedUnchangedMain=True,controlledUnrelatedContracts=True,controlledExternalReaders=True,
        commandCalls=command_calls,realCommonChecks=True)

    receipt=json.loads(args.delivered_receipt.read_text('utf-8-sig'));archive=Path(receipt['archive'])
    check(identity(archive)['sha256']==receipt['sha256'],'delivered v3 ZIP identity matches its receipt')
    comparisons=[]
    archive_rows,requirements=closure.archive_index(archive)
    required={row['path'].lower():row for row in requirements}
    baseline_data=json.loads(args.baseline_audit.read_text('utf-8-sig'))
    baseline_archives=[Path(row['path']) for row in baseline_data['archiveEvidence']]
    baseline_indexes=[closure.archive_index(path)[0] for path in baseline_archives]
    with zipfile.ZipFile(archive) as package:
        entries={item.filename.replace('\\','/').lower():item for item in package.infolist() if not item.is_dir()}
        for name in sorted(EXPECTED):
            path=args.common/name;record=identity(path);check(record['bytes']>0,'actual common exists: '+name)
            matches=[entry for key,entry in entries.items() if key.endswith('/'+name.lower()) or key==name.lower()]
            check(len(matches)<=1,'v3 asset unambiguous: '+name)
            member=matches[0] if matches else None
            digest=hashlib.sha256(package.read(member)).hexdigest() if member else None
            need=required.get(('cs/cstrike/'+name).lower())
            baseline_member=None
            if need:
                check(need['sha256']==record['sha256'] and need['bytes']==record['bytes'],'v3 baseline declaration matches common: '+name)
                candidates=[index[('cs/cstrike/'+name).lower()] for index in baseline_indexes if ('cs/cstrike/'+name).lower() in index]
                check(bool(candidates),'required baseline has actual prior ZIP member: '+name)
                baseline_member=candidates[-1]
                with zipfile.ZipFile(baseline_member['archive']) as prior:
                    data=prior.read(baseline_member['member'])
                check(len(data)==need['bytes'] and hashlib.sha256(data).hexdigest()==need['sha256'],'prior ZIP actual bytes verify baseline: '+name)
            comparisons.append(dict(resource=name,common=record,zipMember=None if member is None else member.filename,
                zipSha256=digest,alreadySame=bool(member and digest==record['sha256']),
                requiredBaseline=need,baselineMember=baseline_member,
                deployment='same-in-v3' if member and digest==record['sha256'] else 'replace-v3' if member else
                    'same-verified-required-baseline' if need else 'new-resource-required'))
    evidence.update(resourceTool=identity(source/'tools/csol_scenario_resources.py'),closureTool=identity(script),
        header=identity(header),selectedSession=identity(source/'native/csol/server/scenario_session.cpp'),
        oldSession=identity(args.old_source/'native/csol/server/scenario_session.cpp'),archive=identity(archive),baselineAudit=identity(args.baseline_audit))
    result=dict(success=True,checks=len(checks),labels=checks,expectedRejections=failures,evidence=evidence,
        resources=comparisons,resourceCopy=False,compiled=False,deployed=False,
        boundary='Real respawn contract/fixed-precache/main resource merge; unrelated main contracts/readers controlled, not full map collector')
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(checks=len(checks),resources=[{'resource':r['resource'],'deployment':r['deployment']} for r in comparisons]),ensure_ascii=False,indent=2))

if __name__=='__main__':main()
