"""Audit actual melee precache calls and production-parsed model dependencies.

Writes a plan, never deploys resources. A missing WAV can be represented by an
empty existence probe only inside this diagnostic directory so the existing
collector can enumerate later references. Such probes are never export inputs,
have no source SHA in the plan, and always leave closureComplete false.
"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parent.parent


def identity(path):
    data=path.read_bytes()
    return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())


def body(text,signature):
    start=text.index(signature);at=text.index('{',start);depth=1;end=at+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[at+1:end-1],text[:at].count('\n')+1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common',type=Path,required=True)
    parser.add_argument('--collector',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    common=args.common.resolve();out=args.output.resolve();collector=args.collector.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace build output')
    out.mkdir(parents=True)
    server=ROOT/'native/csol/server/weapons/wpn_melee.cpp'
    client=ROOT/'native/csol/client/weapons/melee.cpp'
    header=ROOT/'native/csol/weapons/melee_resources.hpp'
    hud_header=ROOT/'native/csol/weapons/melee_hud_resources.hpp'
    records={};edges=[];inputs={};audits=[]
    def read(path):
        data=path.read_bytes();key=str(path).lower()
        entry=dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        if key in inputs and inputs[key]!=entry:raise ValueError('Input changed during audit: '+str(path))
        inputs[key]=entry;return data
    text=read(server).decode('utf8');head=read(header).decode('utf8');client_text=read(client).decode('utf8')
    hud_text=read(hud_header).decode('utf8')
    generated_hud={}
    for variant,name in (('DragonSword','dragonsword'),('SkullAxe','skullaxe')):
        match=re.search(r'\b'+variant+r'HUD\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);',hud_text)
        if not match:raise ValueError('Missing generated knife HUD '+variant)
        content=''.join(ast.literal_eval(token) for token in re.findall(r'"(?:\\.|[^"\\])*"',match[1]))
        generated_hud['sprites/weapon_'+name+'.txt']=content
    for path in (collector,Path(__file__)):read(path)
    # resource_list.cpp reads every weapon alias JSON. dependencies.cpp reads
    # common materials even when no row applies to the requested model. Keep
    # these as parsing inputs, not unrelated export dependencies.
    aliases=lambda: sorted(path for path in (common/'weapons').glob('*.json')
        if path.is_file() and path.suffix=='.json')
    alias_inputs=aliases();context_inputs=list(alias_inputs)
    common_materials=common/'materials/common.json'
    if common_materials.is_file():context_inputs.append(common_materials)
    for path in context_inputs:read(path)
    def safe(name):
        if name.startswith(('/', '\\')) or ':' in name or '\\' in name or '..' in name.split('/'):
            raise ValueError('Unsafe resource path '+name)
        return name
    def add(destination,reason,source=None,project=None,generated=None):
        safe(destination)
        if destination in generated_hud:
            if generated is not None and generated!=generated_hud[destination]:
                raise ValueError('Collector generated HUD differs from source '+destination)
            generated=generated_hud[destination]
        source=source or (destination.replace('sprites/csol/','sprites/',1) if destination.startswith('sprites/csol/') else destination)
        path=project or common/safe(source)
        exists=path.is_file()
        if generated is not None:
            data=generated.encode('utf8');entry=dict(destination=destination,sourceKind='generated-descriptor',source=None,commonSource=None,exists=True,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        elif exists:
            data=read(path);entry=dict(destination=destination,sourceKind='project-config' if project else 'common',source=str(path),commonSource=None if project else source,exists=True,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
        else:
            entry=dict(destination=destination,sourceKind='common',source=str(path),commonSource=source,exists=False,bytes=None,sha256=None)
        old=records.get(destination)
        if old and any(old.get(k)!=entry.get(k) for k in ('sha256','source','commonSource')):raise ValueError('Conflicting source '+destination)
        records[destination]=entry
        edges.append(dict(consumer=reason,resource=destination))
    fields=re.findall(r'const char\*\s+(\w+);',body(head,'struct Resources')[0])
    variants={}
    for variant in ('DragonSword','SkullAxe'):
        values=re.findall(r'"([^"\n]+)"',re.search(r'\b'+variant+r'Resources\s*\{([^}]+)\}',head,re.S)[1])
        if len(values)!=len(fields):raise ValueError('Unknown resource initializer')
        variants[variant]=dict(zip(fields,values))
    sounds={}
    for variant in variants:
        sounds[variant]=re.findall(r'"([^"\n]+)"',re.search(r'\b'+variant+r'Sounds\s*\{([^}]+)\}',head,re.S)[1])
    precache,startline=body(text,'void CCSOLMelee::Precache()')
    branch=list(variants);call_count=0
    for relative,line in enumerate(precache.splitlines(),1):
        if 'if (Variant() == Variant::DragonSword)' in line:branch=['DragonSword']
        elif '} else {' in line:branch=['SkullAxe']
        if 'PRECACHE_' not in line:continue
        call_count+=1;reason=f'native/csol/server/weapons/wpn_melee.cpp:{startline+relative}'
        literal=re.search(r'PRECACHE_(MODEL|GENERIC|SOUND)\("([^"\n]+)"\)',line)
        field=re.search(r'PRECACHE_(MODEL|GENERIC)\(const_cast<char\*>\(resource\.(\w+)\)\)',line)
        loop=re.search(r'for \(auto\* sound : (DragonSword|SkullAxe)Sounds\) PRECACHE_SOUND\(const_cast<char\*>\(sound\)\)',line)
        if literal:add(('sound/' if literal[1]=='SOUND' else '')+literal[2],reason)
        elif field:
            for variant in branch:add(variants[variant][field[2]],reason+' '+variant)
        elif loop:
            for name in sounds[loop[1]]:add('sound/'+name,reason+' '+loop[1])
        else:raise ValueError('Unresolved actual precache expression: '+line)
    if call_count!=13:raise ValueError('Precache source changed: review call coverage')
    for variant,resource in variants.items():
        add(resource['damageConfig'],'LOAD_FILE_FOR_ME(resource.damageConfig) '+variant,
            project=ROOT/'native/csol/config'/Path(resource['damageConfig']).name)
    # Also audit hardcoded attack sounds and the client's actual effect lookup.
    for filename,code in ((server,text),(client,client_text)):
        for match in re.finditer(r'"((?:models/|sprites/|weapons/|events/)[^"\n]+\.(?:mdl|spr|wav|sc))"',code):
            name=match[1];name='sound/'+name if name.startswith('weapons/') else name
            add(name,str(filename.relative_to(ROOT))+':'+str(code[:match.start()].count('\n')+1))
    event_calls=re.findall(r'\b(?:PRECACHE_EVENT|PLAYBACK_EVENT(?:_FULL)?)\s*\(',text+'\n'+client_text)
    if event_calls:raise ValueError('New event-script calls need explicit argument resolution')
    # The resource table has localized comments in a legacy encoding. Its
    # relevant identifiers and coordinates are ASCII; decode matched rows only.
    hud=common/'sprites/hud.txt';hud_data=read(hud)
    hud_rows=[]
    for variant,resource in variants.items():
        path=common/resource['originalHud'];lines=read(path).decode('ascii').splitlines()
        sprite,x,y,w,h=lines[0].split();symbol=lines[1].strip()
        if symbol!=resource['deathIcon']:raise ValueError('HUD death symbol mismatch')
        name=resource['classname']
        card=['640','csol/'+sprite,x,y,w,h]
        generated=generated_hud['sprites/'+name+'.txt'].splitlines()
        if len(generated)!=3 or generated[0]!='2' or generated[1].split()!=['weapon',*card] or generated[2].split()!=['weapon_s',*card]:
            raise ValueError('Generated knife HUD does not preserve the original card '+variant)
        add('sprites/csol/'+sprite+'.spr',resource['originalHud']+' selection card')
        matches=[(i,row.decode('ascii').split()) for i,row in enumerate(hud_data.splitlines(),1)
            if row.split() and row.split()[0]==symbol.encode('ascii')]
        if len(matches)!=1:raise ValueError('Missing or ambiguous actual death HUD row')
        line,row=matches[0]
        if len(row)!=7:raise ValueError('Unsupported HUD row')
        add('sprites/csol/'+row[2]+'.spr','sprites/hud.txt:'+str(line)+' '+symbol)
        hud_rows.append(dict(variant=variant,selection=dict(atlas=sprite,rect=list(map(int,(x,y,w,h)))),
            death=dict(symbol=symbol,resolution=int(row[1]),atlas=row[2],rect=list(map(int,row[3:])))))
    models=sorted(n for n in records if n.endswith('.mdl'))
    missing_sounds=set();model_metadata=[];omitted_events=[]
    for model in models:
        raw=read(common/model)
        if raw[:4]!=b'IDST' or struct.unpack_from('<i',raw,4)[0]!=10:raise ValueError('Model needs codec before independent event audit')
        word=lambda offset:struct.unpack_from('<i',raw,offset)[0]
        name=lambda offset,size:raw[offset:offset+size].split(b'\0')[0].decode('ascii')
        if word(72)!=len(raw):raise ValueError('Bad model length')
        events=[]
        for sequence in range(word(164)):
            at=word(168)+sequence*176
            for index in range(word(at+48)):
                event=word(at+52)+index*76;kind=word(event+4);option=name(event+12,64)
                events.append(dict(sequence=sequence,id=kind,frame=word(event),options=option,offset=event))
                if option.lower().endswith('.wav'):
                    sample=option[3:] if kind==5005 else option
                    if (model.lower()=='models/v_dragonsword.mdl' and kind==5004 and
                            sample.replace('\\','/').lower()=='weapons/dragonsword_stab.wav'):
                        omitted_events.append(dict(model=model,sequence=sequence,event=kind,
                            sample=sample,reason='User-approved omission of archive-absent original cue'))
                        continue
                    add('sound/'+sample,model+f' event {kind} seq {sequence} offset {event}')
                    if not (common/'sound'/sample).is_file():missing_sounds.add('sound/'+sample)
        textures=[dict(name=name(word(184)+index*80,64),offset=word(184)+index*80) for index in range(word(180))]
        model_metadata.append(dict(model=model,events=events,textures=textures))
    # Fail honestly against the untouched common root first. Only diagnostic
    # WAV existence probes may unblock enumeration of references after a miss.
    probe_root=out/'planning-only-missing-inputs'
    for number,model in enumerate(models,1):
        command=[str(collector),str(common),'--relocatable-json',model]
        run=subprocess.run(command,capture_output=True,timeout=90)
        (out/f'{number:02}-initial.stdout').write_bytes(run.stdout)
        (out/f'{number:02}-initial.stderr').write_bytes(run.stderr)
        audit=dict(model=model,initialExit=run.returncode,initialCommand=command)
        if run.returncode:
            error=run.stderr.decode('utf8','replace')
            missing=re.findall(r'FAIL missing weapon dependency: ([^\r\n]+)',error)
            if len(missing)!=1 or missing[0] not in missing_sounds:raise RuntimeError(error)
            for sample in missing_sounds:
                placeholder=probe_root/sample;placeholder.parent.mkdir(parents=True,exist_ok=True)
                placeholder.write_bytes(b'')
            command=[str(collector),str(common),'--overlay',str(probe_root),'--relocatable-json',model]
            run=subprocess.run(command,capture_output=True,timeout=90)
            (out/f'{number:02}-enumeration.stdout').write_bytes(run.stdout)
            (out/f'{number:02}-enumeration.stderr').write_bytes(run.stderr)
            audit.update(enumerationCommand=command,enumerationExit=run.returncode,diagnosticMissing=sorted(missing_sounds))
        if run.returncode:raise RuntimeError(run.stderr.decode('utf8','replace'))
        dependencies=json.loads(run.stdout)
        for dependency in dependencies:
            add(dependency['path'],model+' production collector transitive parse',dependency.get('source'),generated=dependency.get('content'))
        audit['dependencies']=len(dependencies);audits.append(audit)
    missing=[row for row in records.values() if not row['exists']]
    changed=[x for x in inputs.values() if identity(Path(x['path']))!=x]
    if changed:raise ValueError('Inputs changed during audit')
    if aliases()!=alias_inputs or common_materials.is_file()!=(common_materials in context_inputs):
        raise ValueError('Collector context inventory changed during audit')
    if any(Path(row['source']).exists() for row in missing):raise ValueError('Missing input appeared during audit')
    result=dict(closureComplete=not missing,deployable=False,precacheCalls=call_count,eventScriptCalls=0,
        sources=[inputs[str(p).lower()] for p in (server,client,header,hud_header)],collector=identity(collector),
        resources=sorted(records.values(),key=lambda x:x['destination']),dependencyEdges=edges,
        modelMetadata=model_metadata,hudRows=hud_rows,collectorRuns=audits,omittedEvents=omitted_events,
        collectorContextInputs=[str(path) for path in context_inputs],
        missing=missing,inputs=list(inputs.values()),inputsUnchanged=True,
        limitations=['No runtime files were deployed or assets exported.',
            'Missing WAV probes exist only to enumerate the remaining dependency graph; they are never valid resources.',
            'Knife descriptors are converted to two CS selection roles using the same original card; resource validation is not gameplay HUD verification.',
            'Common material tables are not relocated or converted by this audit; final export must use existing material staging when required.'])
    (out/'resource-closure.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(resources=len(records),inputs=len(inputs),missing=[x['destination'] for x in missing],closureComplete=not missing)))


if __name__=='__main__':main()
