"""Rebuild an embedded bundle from its shipped inventory and audited runtime.

All previous assets are checked, not only a particular missing engine symbol.
The resulting bundle identity binds every static file and installation order.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import zipfile

spec=importlib.util.spec_from_file_location('launcher_package',Path(__file__).with_name('package.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
sha,plain=base.digest,base.plain


def refresh(previous,runtime,catalog_audit,publication,output,version):
    previous,runtime,catalog_audit,publication,output=map(plain,(previous,runtime,catalog_audit,publication,output))
    if output.exists() or not runtime.is_relative_to(base.ROOT) or not output.is_relative_to(base.ROOT):
        raise ValueError('Use a new workspace output and an isolated runtime')
    cfg=json.loads((previous/'bundle.json').read_text('utf-8'))
    audit=json.loads(catalog_audit.read_text('utf-8'))
    if audit['invalidIndexes'] or not audit['crcFunctionBytesMatch']:
        raise ValueError('Catalog audit is incomplete')
    import base64
    signed=json.loads(base64.b64decode(json.loads(publication.read_text('utf-8-sig'))['payload']))
    if signed['bundleId']!=cfg['bundleId'] or len(signed['files'])!=19:
        raise ValueError('Wrong previous deployed core contract')
    core={r['path']:r for r in signed['files']}
    if set(core)!=set(cfg['corePaths']):raise ValueError('Core allowlist changed')
    for name,digest in [('hw.dll',cfg['engineSha256']),('cstrike/cl_dlls/client.dll',cfg['clientSha256'])]:
        if sha(runtime/name)!=digest:raise ValueError('Unsupported base game identity')
    plugins=[line.split('//')[0].strip() for line in (runtime/'cstrike/metahook/configs/plugins.lst').read_text('utf-8-sig').splitlines()]
    plugins=[line for line in plugins if line and not line.startswith(('#',';'))]
    if plugins!=cfg['plugins']:raise ValueError('Plugin order changed; review separately')
    rows=[];data_by_path={};changes=[]
    with zipfile.ZipFile(previous/'payload.zip') as archive:
        if len(archive.namelist())!=len(cfg['files']) or set(archive.namelist())!={r['path'] for r in cfg['files']}:
            raise ValueError('Previous embedded inventory mismatch')
        for old in cfg['files']:
            name=old['path'];old_data=archive.read(name)
            if len(old_data)!=old['bytes'] or hashlib.sha256(old_data).hexdigest()!=old['sha256']:
                raise ValueError('Previous embedded asset corrupt')
            source=plain(runtime/name)
            data=old_data if name.startswith('goldcraft-licenses/') else source.read_bytes()
            row=dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
            if name in core and row!=core[name]:raise ValueError('Unverified core input: '+name)
            if row!=old:changes.append(dict(path=name,oldSha256=old['sha256'],newSha256=row['sha256'],bytes=row['bytes'],core=name in core))
            rows.append(row);data_by_path[name]=data
    indexed={r['path']:r for r in rows}
    catalogs=list((runtime/'cstrike/metahook/gamedata').rglob('*.json'))
    if len(catalogs)!=audit['catalogCount'] or len(audit['catalogs'])!=len(catalogs):raise ValueError('Catalog inventory changed')
    for expected in audit['catalogs']:
        if indexed.get(expected['path'])!=expected:raise ValueError('Audited catalog differs from bundle: '+expected['path'])
    # Inventory newly added runtime resources in every tree used by package.py.
    trees={'metahook/dlls':{'.dll'},'metahook/gamedata':{'.json'},'metahook/shaders':{'.glsl','.fsh','.vsh','.h'},
           'renderer/shader':{'.glsl','.fsh','.vsh','.h'},'renderer/texture':{'.dds','.tga','.png'},
           'captionmod':{'.txt','.json','.csv','.res','.ttf','.otf','.tga','.png'},'bulletphysics':{'.txt','.json','.cfg','.res'},
           'studioevents':{'.txt','.json'},'vgui2ext':{'.txt','.json','.res','.ttf','.otf'}}
    for directory,extensions in trees.items():
        for path in (runtime/'cstrike'/directory).rglob('*'):
            plain(path)
            if path.is_file() and path.suffix.lower() in extensions and not path.name.endswith('_cache.txt'):
                if path.relative_to(runtime).as_posix() not in indexed:raise ValueError('New static resource requires explicit inventory review: '+str(path))
    fixed={r['path']:r['sha256'] for r in rows if r['path'] not in core}
    identity=hashlib.sha256(json.dumps(dict(files=fixed,plugins=plugins,core=cfg['corePaths']),sort_keys=True,separators=(',',':')).encode()).hexdigest()
    if identity==cfg['bundleId']:raise ValueError('No static contract change')
    old_id=cfg['bundleId'];cfg.update(bundleId=identity,version=version,files=rows)
    output.mkdir(parents=True)
    with zipfile.ZipFile(output/'payload.zip','x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name,data in data_by_path.items():archive.writestr(name,data)
    with zipfile.ZipFile(output/'payload.zip') as archive:
        for row in rows:
            with archive.open(row['path']) as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
            if digest!=row['sha256']:raise ValueError('Embedded payload verification failed')
    (output/'bundle.json').write_text(json.dumps(cfg,ensure_ascii=False,indent=2)+'\n','utf-8')
    receipt=dict(previousBundleId=old_id,bundleId=identity,files=len(rows),coreFiles=len(core),changes=changes,
                 allCatalogsVerified=len(catalogs),missingFiles=[],additionalUnreviewedFiles=[],
                 sourceEvidence={str(p):sha(p) for p in [previous/'bundle.json',previous/'payload.zip',catalog_audit,publication]})
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({k:v for k,v in receipt.items() if k!='sourceEvidence'},ensure_ascii=False))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('previous','runtime','catalog-audit','publication','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--version',required=True);a=p.parse_args()
    refresh(a.previous,a.runtime,a.catalog_audit,a.publication,a.output,a.version)
