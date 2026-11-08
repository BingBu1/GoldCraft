"""Package the complete signed client publication without server game resources."""
from pathlib import Path
import argparse
import base64
import hashlib
import importlib.util
import json
import shutil
import zipfile

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('launcher_package',ROOT/'tools/client-launcher/package.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)


def package(publication,output):
    publication,output=map(base.plain,(publication,output))
    if output.exists() or not output.is_relative_to(ROOT/'server-packages'):
        raise ValueError('Use a fresh workspace server-packages directory')
    manifest=publication/'manifest.json'
    if base.digest(manifest)!='f41fd468156fca0f3b67bf4e351b488024cabfa1f4ae3b52c1a624721e7484df':
        raise ValueError('Wrong signed publication')
    release=json.loads(base64.b64decode(json.loads(manifest.read_text())['payload']))
    if release['sequence']!=2026110801 or len(release['files'])!=19:raise ValueError('Wrong release')
    files={'payload/cs/launcher-public/manifest.json':manifest,
           'Files.ps1':ROOT/'tools/scenario-update/Files.ps1',
           'release-public-key.xml':ROOT/'tools/respawn-release/release-public-key.xml'}
    for name in ('Update-Launcher.ps1','Update-Launcher.cmd','Restore-Launcher.cmd','README.md'):
        files[name]=Path(__file__).with_name(name)
    for row in release['files']:
        rel='blobs/'+row['sha256']+'/'+Path(row['path']).name
        source=base.plain(publication/rel)
        if base.digest(source)!=row['sha256'] or source.stat().st_size!=row['bytes']:raise ValueError('Corrupt core DLL')
        files['payload/cs/launcher-public/'+rel]=source
    stage=output/'BingSang-Launcher-Server-20261108';stage.mkdir(parents=True)
    rows=[]
    for rel,source in files.items():
        source=base.plain(source);dest=base.plain(stage/rel);dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,dest)  # DLLs/scripts stay independent of mutable build inputs.
        digest=base.digest(source)
        if base.digest(dest)!=digest or dest.stat().st_nlink!=1:raise ValueError('Package copy mismatch')
        rows.append(dict(path=rel,bytes=dest.stat().st_size,sha256=digest))
    archive=output/(stage.name+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for row in rows:z.write(stage/row['path'],stage.name+'/'+row['path'])
    with zipfile.ZipFile(archive) as z:
        if len(z.infolist())!=len(rows):raise ValueError('Unexpected archive member')
        for row in rows:
            with z.open(stage.name+'/'+row['path']) as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
            if sha!=row['sha256']:raise ValueError('ZIP hash mismatch')
    receipt=dict(bundleId=release['bundleId'],sequence=release['sequence'],coreFiles=19,files=rows,
                 zip=dict(path=str(archive),bytes=archive.stat().st_size,sha256=base.digest(archive)))
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n','utf-8')
    print(json.dumps(receipt['zip']))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--publication',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();package(a.publication,a.output)
