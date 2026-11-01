"""Prepare a single-EXE payload from the verified MetaHook resource inventory.

The old complete-resource ZIP is an inventory, not the DLL version source.
Current isolated runtime files supply every byte. Game assets remain on FastDL.
"""
from pathlib import Path
import argparse, hashlib, json, os, stat, zipfile

ROOT = Path(__file__).resolve().parents[2]
def digest(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def plain(p):
    p = Path(os.path.abspath(p))
    for q in (p, *p.parents):
        if q.exists() and q.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Reparse point: ' + str(q))
    return p

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--runtime',type=Path,required=True)
    ap.add_argument('--inventory-zip',type=Path,required=True)
    ap.add_argument('--deployed-resource-zip',type=Path,required=True)
    ap.add_argument('--public-key',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--server-url',required=True)
    ap.add_argument('--game-address',required=True)
    ap.add_argument('--version',default='2026.11.01')
    a=ap.parse_args();game=plain(a.runtime);out=plain(a.output)
    if not game.is_relative_to(ROOT) or not out.is_relative_to(ROOT):raise ValueError('Runtime and outputs must be isolated in workspace')
    if out.exists():raise ValueError('Use a new payload directory')
    rows={}
    def add(path,source):
        source=plain(source)
        if path in rows: return
        if not source.is_file():raise FileNotFoundError(source)
        if path.endswith('.pdb'):raise ValueError('No debug symbols in launcher')
        rows[path]=dict(path=path,bytes=source.stat().st_size,sha256=digest(source),source=str(source))
    with zipfile.ZipFile(plain(a.inventory_zip)) as z:
        name=next(n for n in z.namelist() if n.endswith('/package.json'))
        old=json.loads(z.read(name))
        for row in old['files']:
            path=row['path']
            if path.startswith('install/output/'):
                path=path[len('install/output/'):]
                if path.startswith('svencoop/'):path='cstrike/'+path[len('svencoop/'):]
                add(path,game/path)
    with zipfile.ZipFile(plain(a.deployed_resource_zip)) as z:
        deployed=json.loads(z.read(next(n for n in z.namelist() if n.endswith('/package.json'))))['deployedPostContract']
    if digest(game/'cstrike/metahook/plugins/GoldCraft.dll')!=deployed['goldcraftSha256']:
        raise ValueError('Supply the post-resource contract matching the deployed GoldCraft DLL')
    for name,expected in deployed['resources'].items():
        name=name.replace('install/output/svencoop/','cstrike/',1)
        if digest(plain(game/name))!=expected:raise ValueError('Deployed post resource mismatch: '+name)
        add(name,game/name)
    # Preserve the actual installed ordering; no legacy GoldCraft adjacency rewrite.
    plugins=[]
    for line in (game/'cstrike/metahook/configs/plugins.lst').read_text(encoding='utf-8-sig').splitlines():
        name=line.split('//')[0].strip()
        if name and not name.startswith(('#',';')):plugins.append(name)
    for plugin in plugins:add('cstrike/metahook/plugins/'+plugin,game/'cstrike/metahook/plugins'/plugin)
    add('cstrike/metahook/plugins/GoldCraftSteamWorker.exe',game/'cstrike/metahook/plugins/GoldCraftSteamWorker.exe')
    if (game/'MetaHook.dll').exists():add('MetaHook.dll',game/'MetaHook.dll')
    trees={
        'metahook/dlls':{'.dll'},'metahook/gamedata':{'.json'},'metahook/shaders':{'.glsl','.fsh','.vsh','.h'},
        'renderer/shader':{'.glsl','.fsh','.vsh','.h'},'renderer/texture':{'.dds','.tga','.png'},
        'captionmod':{'.txt','.json','.csv','.res','.ttf','.otf','.tga','.png'},
        'bulletphysics':{'.txt','.json','.cfg','.res'},'studioevents':{'.txt','.json'},
        'vgui2ext':{'.txt','.json','.res','.ttf','.otf'},
    }
    for directory,extensions in trees.items():
        for p in sorted((game/'cstrike'/directory).rglob('*')):
            plain(p)
            if p.is_file() and p.suffix.lower() in extensions and not p.name.endswith('_cache.txt'):
                add(p.relative_to(game).as_posix(),p)
    for p in (ROOT/'notices').glob('*.txt'):add('goldcraft-licenses/'+p.name,p)
    add('goldcraft-licenses/THIRD_PARTY_NOTICES.md',ROOT/'THIRD_PARTY_NOTICES.md')
    core=sorted(p for p in rows if p.endswith('.dll') and (p=='MetaHook.dll' or p.startswith('cstrike/metahook/plugins/') or p.startswith('cstrike/metahook/dlls/')))
    # Core update capability is limited to this exact embedded allowlist.
    fixed={p:r['sha256'] for p,r in rows.items() if p not in core}
    identity=hashlib.sha256(json.dumps(dict(files=fixed,plugins=plugins,core=core),sort_keys=True,separators=(',',':')).encode()).hexdigest()
    config=dict(schema=1,bundleId=identity,version=a.version,updateBase=a.server_url,gameAddress=a.game_address,
                publicKeyPem=plain(a.public_key).read_text(encoding='ascii'),
                engineSha256=digest(game/'hw.dll'),clientSha256=digest(game/'cstrike/cl_dlls/client.dll'),
                plugins=plugins,dllPaths=['FreeImage'],corePaths=core,
                files=[{k:v for k,v in row.items() if k!='source'} for _,row in sorted(rows.items())])
    out.mkdir(parents=True)
    with zipfile.ZipFile(out/'payload.zip','x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for path,row in sorted(rows.items()):
            source=Path(row['source'])
            if digest(source)!=row['sha256']:raise ValueError('Runtime changed while packaging: '+path)
            z.write(source,path)
    (out/'bundle.json').write_text(json.dumps(config,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'sources.json').write_text(json.dumps(list(rows.values()),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(files=len(rows),coreDlls=len(core),bundleId=identity,payloadBytes=(out/'payload.zip').stat().st_size),indent=2))
if __name__=='__main__':main()
