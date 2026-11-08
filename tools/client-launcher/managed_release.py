"""Prepare a complete v2 DLL/catalog/shader release for the existing HTTP routes."""
from pathlib import Path
import argparse
import base64
import gzip
import importlib.util
import json
import re
import struct

spec=importlib.util.spec_from_file_location('launcher_package',Path(__file__).with_name('package.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
RESOURCE=re.compile(r'cstrike/(?:metahook/gamedata/[A-Za-z0-9_./-]+\.json|(?:metahook/shaders|renderer/shader)/[A-Za-z0-9_./-]+\.(?:glsl|fsh|vsh|h))\Z')
CORE=re.compile(r'(?:MetaHook\.dll|cstrike/metahook/(?:plugins|dlls)/[A-Za-z0-9_/-]+\.dll)\Z')


def prepare(bundle,runtime,sequence,version,address):
    bundle,runtime=map(base.plain,(bundle,runtime))
    cfg=json.loads(bundle.read_text('utf-8-sig'))
    if sequence<1 or not version or len(version)>80 or not re.fullmatch(r'[A-Za-z0-9.-]+:[0-9]{1,5}',address) or not 1<=int(address.rsplit(':',1)[1])<=65535:
        raise ValueError('Invalid release identity')
    audit=[]
    def read(path):
        path=base.plain(path);data=path.read_bytes()
        import hashlib
        audit.append(dict(source=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
        return data,audit[-1]
    for name,digest in [('hw.dll',cfg['engineSha256']),('cstrike/cl_dlls/client.dll',cfg['clientSha256'])]:
        if read(runtime/name)[1]['sha256']!=digest:raise ValueError('Unsupported base game')
    for row in cfg['files']:
        if row['path'] in cfg['corePaths'] or RESOURCE.fullmatch(row['path']) or row['path'].startswith('goldcraft-licenses/'):continue
        _,actual=read(runtime/row['path'])
        if (actual['sha256'],actual['bytes'])!=(row['sha256'],row['bytes']):raise ValueError('Bootstrap resource changed outside managed scope: '+row['path'])
    data,_=read(runtime/'cstrike/metahook/configs/plugins.lst')
    plugins=[line.split('//')[0].strip() for line in data.decode('utf-8-sig').splitlines()]
    plugins=[name for name in plugins if name and not name.startswith(('#',';'))]
    if len(plugins)>64 or len({p.lower() for p in plugins})!=len(plugins) or not set(cfg['plugins'])<=set(plugins) or any(not re.fullmatch(r'[A-Za-z0-9_-]+\.dll',p) for p in plugins):raise ValueError('Invalid required plugin order')
    cores=set(cfg['corePaths'])|{'cstrike/metahook/plugins/'+p for p in plugins}
    for p in (runtime/'cstrike/metahook/dlls').rglob('*.dll'):cores.add(p.relative_to(runtime).as_posix())
    files=[];sources=[]
    for name in sorted(cores):
        if not CORE.fullmatch(name):raise ValueError('Unsupported core path')
        data,actual=read(runtime/name)
        if not 64<=len(data)<16*1024*1024 or data[:2]!=b'MZ':raise ValueError('Invalid core size or PE')
        offset=struct.unpack_from('<I',data,60)[0]
        if offset<64 or offset+6>len(data) or data[offset:offset+6]!=b'PE\0\0L\x01':raise ValueError('Core must be x86 PE')
        if len(Path(name).name)>81:raise ValueError('Core filename exceeds existing HTTP route')
        row=dict(path=name,bytes=actual['bytes'],sha256=actual['sha256']);files.append(row);sources.append(dict(row,source=actual['source']))
    if len(files)>128 or sum(row['bytes'] for row in files)>256*1024*1024:raise ValueError('Core release exceeds bounds')
    resources=[]
    for directory in ('cstrike/metahook/gamedata','cstrike/metahook/shaders','cstrike/renderer/shader'):
        if not (runtime/directory).is_dir():raise ValueError('Managed resource directory is missing')
        for path in sorted((runtime/directory).rglob('*')):
            base.plain(path)
            if not path.is_file():continue
            if directory=='cstrike/renderer/shader' and path.name.endswith('_cache.txt'):continue
            name=path.relative_to(runtime).as_posix()
            if not RESOURCE.fullmatch(name):raise ValueError('Unmanaged file in managed resource tree: '+name)
            data,actual=read(path)
            if not 0<len(data)<=4*1024*1024:raise ValueError('Resource size outside bounds')
            resources.append(dict(path=name,bytes=len(data),sha256=actual['sha256'],gzip=base64.b64encode(gzip.compress(data,mtime=0)).decode()))
    if len(resources)>1024 or sum(r['bytes'] for r in resources)>32*1024*1024:raise ValueError('Resource set exceeds bounds')
    indexed={r['path']:r for r in resources}
    # Check all symbol indexes against the exact bytes paired with the DLLs.
    for row in resources:
        if not row['path'].startswith('cstrike/metahook/gamedata/'):continue
        data=json.loads(gzip.decompress(base64.b64decode(row['gzip'])).decode('utf-8-sig'))
        if not row['path'].endswith('/index.json'):continue
        for entry in data['versions']:
            url=entry['url']
            if '/' in url or '\\' in url or not re.fullmatch(r'[A-Za-z0-9_-]+\.json',url):raise ValueError('Invalid catalog reference')
            linked=indexed.get(str(Path(row['path']).parent/ url).replace('\\','/'))
            if not linked or (linked['sha256'],linked['bytes'])!=(entry['sha256'],entry['size']):raise ValueError('Symbol index does not match published snapshot')
    release=dict(schema=2,bundleId=cfg['bundleId'],sequence=sequence,version=version,gameAddress=address,
                 files=files,resources=resources,plugins=plugins,engineSha256=cfg['engineSha256'],clientSha256=cfg['clientSha256'])
    # Allow room for RSA-3072 signature, envelope and base64 wrapping.
    if len(json.dumps(release,separators=(',',':')).encode())*4//3+2048>1024*1024:raise ValueError('Signed resource manifest exceeds existing 1 MiB HTTP limit')
    return dict(release=release,sources=sources,auditFiles=audit)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--sequence',type=int,required=True);p.add_argument('--version',required=True);p.add_argument('--game-address',required=True)
    a=p.parse_args();print(json.dumps(prepare(a.bundle,a.runtime,a.sequence,a.version,a.game_address),separators=(',',':')))
