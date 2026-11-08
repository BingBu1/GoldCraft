"""Validate managed publication against isolated changing runtime inputs."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import struct

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('managed_release',ROOT/'tools/client-launcher/managed_release.py')
publisher=importlib.util.module_from_spec(spec);spec.loader.exec_module(publisher)


def run(output):
    output=output.resolve()
    if output.exists() or not output.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh build output')
    runtime=output/'game';runtime.mkdir(parents=True);checks=[]
    def put(name,data):
        p=runtime/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);return p
    def sha(data):return hashlib.sha256(data).hexdigest()
    pe=bytearray(256);pe[:2]=b'MZ';struct.pack_into('<I',pe,60,128);pe[128:134]=b'PE\0\0L\x01'
    core='cstrike/metahook/plugins/GoldCraft.dll';symbols='cstrike/metahook/gamedata/example/symbols.json'
    data=b'{"symbols":{}}';put('hw.dll',b'engine');put('cstrike/cl_dlls/client.dll',b'client');put('MetaHook.exe',b'bootstrap')
    put(core,pe);put(symbols,data);put('cstrike/metahook/shaders/core.fsh',b'void main(){}')
    put('cstrike/renderer/shader/test.frag.glsl',b'void main(){}');put('cstrike/renderer/shader/test_cache.txt',b'local cache')
    plugins=put('cstrike/metahook/configs/plugins.lst',b'GoldCraft.dll\n')
    def index(body):return json.dumps(dict(versions=[dict(url='symbols.json',sha256=sha(body),size=len(body))])).encode()
    idx=put('cstrike/metahook/gamedata/example/index.json',index(data))
    cfg=dict(bundleId='stable',engineSha256=sha(b'engine'),clientSha256=sha(b'client'),plugins=['GoldCraft.dll'],corePaths=[core],
        files=[dict(path='MetaHook.exe',bytes=9,sha256=sha(b'bootstrap'))])
    config=output/'bundle.json';config.write_text(json.dumps(cfg),'utf-8')
    def prepare():return publisher.prepare(config,runtime,42,'release','localhost:27015')
    def reject(name):
        try:prepare()
        except ValueError:checks.append(name);return
        raise AssertionError(name)
    first=prepare();assert len(first['release']['resources'])==4;checks.append('dotted shader names accepted and local shader caches excluded')
    next_data=b'{"symbols":{"new":1}}';put(symbols,next_data);idx.write_bytes(index(next_data));pe[-1]=42;put(core,pe)
    second=prepare();assert second['release']['bundleId']==first['release']['bundleId'] and second['release']['files']!=first['release']['files'] and second['release']['resources']!=first['release']['resources'];checks.append('new DLL and symbol catalog retain stable launcher baseline')
    put('cstrike/metahook/plugins/ParallelDownload.dll',pe);plugins.write_bytes(b'GoldCraft.dll\nParallelDownload.dll\n')
    third=prepare();assert len(third['release']['files'])==2 and len(third['release']['plugins'])==2;checks.append('new plugin automatically joins complete signed core set and order')
    idx.write_bytes(index(data));reject('stale symbol index rejects new DLL publication');idx.write_bytes(index(next_data))
    put('MetaHook.exe',b'changed bootstrap');reject('unmanaged bootstrap changes rejected');put('MetaHook.exe',b'bootstrap')
    put('hw.dll',b'different engine');reject('wrong base engine rejected');put('hw.dll',b'engine')
    pe[132:134]=b'd\x86';put(core,pe);reject('64-bit core DLL rejected');pe[132:134]=b'L\x01';put(core,pe)
    unsafe=put('cstrike/metahook/shaders/execute.ps1',b'bad');reject('script in managed tree rejected');unsafe.unlink()
    assert prepare()['release']['schema']==2;checks.append('restored complete runtime passes')
    (output/'receipt.json').write_text(json.dumps(dict(passed=True,checks=checks),indent=2)+'\n','utf-8')
    print(len(checks),'publisher checks passed')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);run(p.parse_args().output)
