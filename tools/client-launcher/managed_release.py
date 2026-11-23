"""Prepare a complete v2 DLL/catalog/shader release for the existing HTTP routes."""
from pathlib import Path
import argparse
import base64
import gzip
import hashlib
import importlib.util
import json
import re
import struct

spec=importlib.util.spec_from_file_location('launcher_package',Path(__file__).with_name('package.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
RESOURCE=re.compile(r'cstrike/(?:metahook/gamedata/[A-Za-z0-9_./-]+\.json|(?:metahook/shaders|renderer/shader)/[A-Za-z0-9_./-]+\.(?:glsl|fsh|vsh|h))\Z')
CORE=re.compile(r'(?:MetaHook\.dll|cstrike/metahook/(?:plugins|dlls)/[A-Za-z0-9_/-]+\.dll)\Z')
DEFERRED_COMPOSITE = 'cstrike/renderer/shader/dfinal_shader.frag.glsl'
DEFERRED_GAMMA_DEFINE = b'#define DFINAL_GAMMA_OUTPUT\n\0'
DEFERRED_GAMMA_EPILOGUE = re.compile(
    rb'(?m)^[ \t]*#[ \t]*ifdef[ \t]+DFINAL_GAMMA_OUTPUT[ \t]*\r?\n'
    rb'\s*out_FragColor\s*=\s*LinearToGamma\s*\(\s*out_FragColor\s*\)\s*;\s*'
    rb'#[ \t]*endif[ \t]*\r?\n\s*}\s*\Z')


def verify_renderer_shader(release, sources):
    """Match Renderer's deferred gamma output variant to the signed shader."""
    source_paths = {row['path']: row['source'] for row in sources}
    for row in release['files']:
        path = row['path']
        if not path.startswith('cstrike/metahook/plugins/') or not Path(path).name.lower().startswith('renderer'):
            continue
        if path not in source_paths:
            raise ValueError('Missing Renderer publication input: ' + path)
        dll = base.plain(source_paths[path]).read_bytes()
        if len(dll) != row['bytes'] or hashlib.sha256(dll).hexdigest() != row['sha256']:
            raise ValueError('Renderer differs from the release being signed: ' + path)
        if DEFERRED_GAMMA_DEFINE in dll:
            break
    else:
        return

    shader = next((row for row in release['resources'] if row['path'] == DEFERRED_COMPOSITE), None)
    if not shader:
        raise ValueError('Missing deferred composite shader for ' + path)
    data = gzip.decompress(base64.b64decode(shader['gzip'], validate=True))
    if len(data) != shader['bytes'] or hashlib.sha256(data).hexdigest() != shader['sha256']:
        raise ValueError('Deferred composite shader differs from the release being signed')
    code = re.sub(rb'/\*.*?\*/|//[^\r\n]*', b'', data, flags=re.DOTALL)
    if not DEFERRED_GAMMA_EPILOGUE.search(code):
        raise ValueError('Deferred composite shader is missing the DFINAL_GAMMA_OUTPUT conversion for ' + path)


# Checked build-10210 producer/consumer pairs in Build-PrecacheGameData.py and
# Build-ClientGameData.py. Older CSOLSupport releases request a subset.
STARTUP_SYMBOLS = {
    'engine': ('6ef7192cd8254e2d', {
        'CSOL_StudioNativeCall': ('patch', 0x240559),
        'CSOL_StudioReadCall': ('patch', 0x24038b),
        'CSOL_StudioRead': ('function', 0x1c80f0),
    }),
    'client': ('124d6034a6b28b40', {
        'GoldCraftTextMenu_Draw': ('function', 0x5c7f0),
        'GoldCraftSniperScope_Draw': ('function', 0x64f90),
        'GoldCraftCofInput_CreateMove': ('function', 0x5b320),
        'GoldCraftCofInput_MaximumSlot': ('global', 0x1179d8),
    }),
}


def verify_catalogs(release, sources):
    """Check indexed snapshots and resolve the issued plugin's reviewed dependencies."""
    needed = {}
    plugin_path = 'cstrike/metahook/plugins/CSOLSupport.dll'
    plugin = next((r for r in release['files'] if r['path'] == plugin_path), None)
    if plugin:
        source = next((r for r in sources if r['path'] == plugin_path), None)
        if not source:
            raise ValueError('Missing CSOLSupport publication input')
        dll = base.plain(source['source']).read_bytes()
        if len(dll) != plugin['bytes'] or hashlib.sha256(dll).hexdigest() != plugin['sha256']:
            raise ValueError('CSOLSupport differs from the release being signed')
        for module, (crc, symbols) in STARTUP_SYMBOLS.items():
            for name, (kind, rva) in symbols.items():
                if name.encode('ascii') + b'\0' in dll:
                    needed[module, crc, name] = kind, rva

    catalogs = {r['path']: r for r in release['resources'] if r['path'].startswith('cstrike/metahook/gamedata/')}
    def decode(row):
        data = gzip.decompress(base64.b64decode(row['gzip'], validate=True))
        if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Corrupt signed catalog: ' + row['path'])
        return json.loads(data.decode('utf-8-sig'))

    indexed = set()
    for path, row in catalogs.items():
        if not path.endswith('/index.json'):
            continue
        for entry in decode(row)['versions']:
            url = entry['url']
            if not re.fullmatch(r'[A-Za-z0-9_-]+\.json', url):
                raise ValueError('Invalid catalog reference')
            linked = catalogs.get(path.rsplit('/', 1)[0] + '/' + url)
            if not linked or (linked['sha256'], linked['bytes']) != (entry['sha256'], entry['size']):
                raise ValueError('Symbol index does not match published snapshot: ' + url)
            indexed.add(linked['path'])
    found = set()
    for path, row in catalogs.items():
        if path.endswith('/index.json'):
            continue
        snapshot = decode(row)
        if path not in indexed or not needed:
            continue
        modules = {m: versions.get('windows', {}).get('crc64', '').lower()
                   for m, versions in snapshot.get('binaries', {}).items()}
        for record in snapshot.get('records', []):
            mod, name = record.get('module'), record.get('symbolName')
            key = mod, modules.get(mod), name
            if record.get('platform') != 'windows' or key not in needed:
                continue
            kind, rva = needed[key]
            field = {'function': 'func_rva', 'patch': 'patch_rva', 'global': 'gv_rva'}[kind]
            if record.get('kind') != kind or int(str(record.get('payload', {}).get(field, -1)), 0) != rva:
                raise ValueError('Incompatible CSOLSupport symbol: ' + name)
            found.add(key)
    missing = sorted(name for _, _, name in needed.keys() - found)
    if missing:
        raise ValueError('Missing CSOLSupport symbols in the signed catalogs: ' + ', '.join(missing))


def prepare(bundle,runtime,sequence,version,address):
    bundle,runtime=map(base.plain,(bundle,runtime))
    cfg=json.loads(bundle.read_text('utf-8-sig'))
    if sequence<1 or not version or len(version)>80 or not re.fullmatch(r'[A-Za-z0-9.-]+:[0-9]{1,5}',address) or not 1<=int(address.rsplit(':',1)[1])<=65535:
        raise ValueError('Invalid release identity')
    audit=[]
    def read(path):
        path=base.plain(path);data=path.read_bytes()
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
    release=dict(schema=2,bundleId=cfg['bundleId'],sequence=sequence,version=version,gameAddress=address,
                 files=files,resources=resources,plugins=plugins,engineSha256=cfg['engineSha256'],clientSha256=cfg['clientSha256'])
    verify_renderer_shader(release, sources)
    verify_catalogs(release, sources)
    # Allow room for RSA-3072 signature, envelope and base64 wrapping.
    if len(json.dumps(release,separators=(',',':')).encode())*4//3+2048>1024*1024:raise ValueError('Signed resource manifest exceeds existing 1 MiB HTTP limit')
    return dict(release=release,sources=sources,auditFiles=audit)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bundle',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--sequence',type=int,required=True);p.add_argument('--version',required=True);p.add_argument('--game-address',required=True)
    a=p.parse_args();print(json.dumps(prepare(a.bundle,a.runtime,a.sequence,a.version,a.game_address),separators=(',',':')))
