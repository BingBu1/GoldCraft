"""Prepare an independent HUD regression proposal. This never invokes a compiler."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
RUNNER_SHA = '855d9126ee15573b45ac44a45606d1d1d792fa07e1856d6787fce2f67cfe8d1c'


def read(path):
    return Path(path).read_text('utf-8-sig')


def identity(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def safe(path):
    path = Path(path).absolute()
    for candidate in (path, *path.parents):
        if candidate.is_symlink() or candidate.is_junction():
            raise ValueError('Reparse path refused: ' + str(candidate))
    return path.resolve()


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def extract(path, signatures):
    raw = Path(path).read_bytes()
    output, spans = [], []
    for signature in signatures:
        needle = signature.encode('utf-8')
        starts = list(re.finditer(rb'(?m)^' + re.escape(needle), raw))
        if len(starts) != 1:
            raise ValueError('Ambiguous production function: ' + signature)
        start = starts[0].start()
        end_match = re.search(rb'\r?\n}\r?\n', raw[start:])
        if not end_match:
            raise ValueError('No function terminator: ' + signature)
        end = start + end_match.end()
        section = raw[start:end]
        output.append(section)
        spans.append(dict(signature=signature, byteStart=start, byteEnd=end,
            firstLine=raw[:start].count(b'\n')+1, lastLine=raw[:end].count(b'\n'),
            bytes=len(section), sha256=hashlib.sha256(section).hexdigest()))
    return b'\n'.join(output), spans


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path, default=ROOT/'build/csol-golden-notice-20261102/prepare-03')
    parser.add_argument('--resources', type=Path, default=ROOT/'build/csol-hud-style-20261102/resources-04')
    args = parser.parse_args()
    out, previous, resources = map(safe, (args.output, args.previous, args.resources))
    if out.exists() or not out.is_relative_to(ROOT/'build'):
        raise ValueError('Use a new workspace build directory')
    plan = json.loads(read(previous/'proposal.json'))
    result = json.loads(read(previous/'result.json'))
    assert result['success'] and result['inputsUnchanged']
    out.mkdir(parents=True)
    source, output = out/'source', out/'output'
    source.mkdir()
    roots = ['tests/csol-hud-style/pure.cpp','tests/csol-hud-style/client.cpp',
        'tests/csol-hud-style/contracts.cpp','tests/csol-hud-style/prepare.py','tests/csol-hud-style/README.md',
        'native/csol/hud_style.cpp','native/csol/image_codec.cpp',
        'native/csol/client/weapons.cpp','native/csol/client/beamgun.cpp']
    pending = [ROOT/name for name in roots]
    sources, copied = [], set()
    while pending:
        original = safe(pending.pop())
        if original in copied:
            continue
        if not original.is_relative_to(ROOT) or not original.is_file():
            raise ValueError('Source missing or outside workspace: ' + str(original))
        relative = original.relative_to(ROOT)
        target = source/relative
        before = identity(original)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        assert target.stat().st_nlink == 1 and not os.path.samefile(original,target)
        assert identity(target)['sha256'] == before['sha256']
        sources.append(dict(relative=relative.as_posix(), original=before, frozen=identity(target)))
        copied.add(original)
        if original.suffix in ('.cpp','.hpp','.h'):
            for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"',read(original),re.M):
                if name.startswith('generated/'):
                    continue
                candidate=next((p for p in (original.parent/name,ROOT/name) if p.is_file()),None)
                if candidate is None:
                    raise ValueError('Unresolved quoted include: ' + name)
                pending.append(candidate)
    # Server TU is audited and sliced, not recompiled with the client SDK.
    server = ROOT/'native/csol/server/characters.cpp'
    safe(server)
    server_before=identity(server)
    target = source/server.relative_to(ROOT)
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(server,target)
    assert target.stat().st_nlink == 1 and not os.path.samefile(server,target)
    assert identity(target)['sha256']==server_before['sha256']==identity(server)['sha256']
    sources.append(dict(relative=server.relative_to(ROOT).as_posix(),original=server_before,frozen=identity(target)))
    generated = source/'tests/csol-hud-style/generated'
    generated.mkdir()
    spans,generated_inputs=[],[]
    native_path=source/'native/csol/client/weapons.cpp'
    native_raw=native_path.read_bytes()
    start=native_raw.index(b'struct NativeVitalRun\r\n') if b'struct NativeVitalRun\r\n' in native_raw else native_raw.index(b'struct NativeVitalRun\n')
    end=native_raw.index(b'int nativeAmmoWeapon{};',start)
    state=native_raw[start:end];state_path=generated/'native_state.inc';state_path.write_bytes(state)
    generated_inputs.append(identity(state_path))
    spans.append(dict(source=identity(native_path),functions=[dict(signature='NativeVitalRun through nativeAmmoDivider declarations',
        byteStart=start,byteEnd=end,firstLine=native_raw[:start].count(b'\n')+1,lastLine=native_raw[:end].count(b'\n'),
        bytes=len(state),sha256=hashlib.sha256(state).hexdigest())],generated=identity(state_path)))
    for name,relative,signatures in [
        ('native_draw.inc','native/csol/client/weapons.cpp',
            ['bool Matches(', 'int MoneySpriteY(', 'bool Inside(', 'void PrepareStyledNativeAmmo(', 'bool HideStyledSprite(', 'void Fill(']),
        ('server_precache.inc','native/csol/server/characters.cpp',['std::string Load(', 'void PrecacheHudStyle(']),
        ('beam_hud.inc','native/csol/client/beamgun.cpp',['void DrawBeamgunHUD('])]:
        raw,functions=extract(source/relative,signatures)
        path=generated/name;path.write_bytes(raw)
        generated_inputs.append(identity(path))
        spans.append(dict(source=identity(source/relative),functions=functions,generated=identity(path)))
    save(out/'production-spans.json',spans)
    aliases={key(row['name']):row for row in json.loads(read(plan['overlay']['path']))['roots']}
    for row in sources:
        suffix='\\'+row['relative'].replace('/','\\').lower()
        for name in list(aliases):
            if name.lower().endswith(suffix):
                aliases[name]['external-contents']=row['frozen']['path']
        for name in (ROOT/row['relative'],source/row['relative']):
            aliases[key(name)]=dict(type='file',name=str(name),**{'external-contents':row['frozen']['path']})
    overlay=out/'source-overlay.json'
    save(overlay,dict(version=0,**{'case-sensitive':False,'use-external-names':False},roots=list(aliases.values())))
    save(out/'source-changes.json',sources)
    resource_inputs=[]
    for path in sorted(resources.rglob('*')):
        safe(path)
        if path.is_file():
            row=identity(path);row['links']=path.stat().st_nlink
            if row['links']!=1:
                raise ValueError('Generated resource has unexpected shared identity: '+str(path))
            resource_inputs.append(row)
    assert len(resource_inputs)==230
    for suffix in ('-manifest.json','-validation.json'):
        resource_inputs.append(identity(resources.with_name(resources.name+suffix)))
    save(out/'resource-inputs.json',resource_inputs)
    flags=[arg.replace(plan['overlay']['path'],str(overlay)) for arg in plan['compileFlags']]
    assert '/clang:'+str(overlay) in flags
    compiler=plan['compiler'];linker=str(Path(compiler).with_name('lld-link.exe'))
    jobs=[]
    for name,relative in [('pure','tests/csol-hud-style/pure.cpp'),('client','tests/csol-hud-style/client.cpp'),
        ('contracts','tests/csol-hud-style/contracts.cpp'),('style','native/csol/hud_style.cpp'),
        ('codec','native/csol/image_codec.cpp'),('weapons-full-tu','native/csol/client/weapons.cpp'),
        ('beamgun-full-tu','native/csol/client/beamgun.cpp')]:
        jobs.append(dict(name='compile-'+name,kind='compile',seconds=120,expectExit=0,
            command=[compiler,*flags,'/showIncludes','-I'+str(source),'/Fo'+str(output/(name+'.obj')),
                '/Fd'+str(output/(name+'.compile.pdb')),'-c','--',str(source/relative)]))
    old_link=next(job for job in plan['jobs'] if job['name']=='link-client')
    libraries=[arg for arg in old_link['command'] if arg.lower().endswith('.lib')]
    for name,objects,arguments in [('pure',['pure','style'],[]),('client',['client','style','codec'],[str(resources)]),
        ('contracts',['contracts','style'],[])]:
        binary=output/('hud-'+name+'.exe')
        jobs.append(dict(name='link-'+name,kind='link',seconds=180,expectExit=0,
            command=[linker,'/nologo','/subsystem:console','/machine:x86','/debug','/incremental:no','/opt:ref',
                '/opt:icf','/opt:lldltojobs=1','/threads:1','/out:'+str(binary),'/pdb:'+str(output/('hud-'+name+'.pdb')),
                *[str(output/(obj+'.obj')) for obj in objects],*libraries]))
        jobs.append(dict(name='run-'+name,kind='run',seconds=90,expectExit=0,
            marker='hud_style_'+name+' checks=',command=[str(binary),*arguments]))
    shutil.copy2(previous/'run.py',out/'run.py')
    assert identity(out/'run.py')['sha256']==RUNNER_SHA
    direct=list(plan['inputs'])+[row['frozen'] for row in sources]+generated_inputs+resource_inputs
    direct.extend(identity(path) for path in (overlay,out/'source-changes.json',out/'production-spans.json',
        out/'resource-inputs.json',out/'run.py',previous/'proposal.json',previous/'result.json',previous/'actual-includes.json'))
    revised=copy.deepcopy(plan)
    revised.update(inputs=list({key(row['path']):row for row in direct}.values()),overlay=identity(overlay),jobs=jobs,
        maxSeconds=900,output=str(output),compileFlags=flags,selectedGroup='hud-style',
        previousProposal=identity(previous/'proposal.json'),previousResult=identity(previous/'result.json'),
        resourceRoot=str(resources),resourceInputs=identity(out/'resource-inputs.json'),productionSpans=identity(out/'production-spans.json'),
        fullTranslationUnits=['client/hud_style.cpp','client/weapons.cpp','client/beamgun.cpp'],
        serverBoundary='Exact Load/PrecacheHudStyle spans, controlled engine file/precache callbacks; not full server DLL',
        reusedObjects=0,compiled=False,deployed=False)
    for name in ('reusedClient','newLocalization','preservedFailedParserAttempt','previousFailure'):
        revised.pop(name,None)
    save(out/'proposal.json',revised)
    for row in sources:
        assert identity(row['original']['path'])['sha256']==row['original']['sha256'], 'Source changed during preparation'
    print(json.dumps(dict(proposal=identity(out/'proposal.json'),independentSources=len(sources),
        jobs=len(jobs),compilerInvoked=False,reusedObjects=0,resources=len(resource_inputs)-2),indent=2))


if __name__=='__main__':
    main()
