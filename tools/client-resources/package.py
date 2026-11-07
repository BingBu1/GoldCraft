"""Create a resources-only cstrike_downloads ZIP without an extracted staging copy.

Reports and hashes remain outside the delivery ZIP. Required source identities
may come from a deployed client resource plan and a captured ReHLDS resource list.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import stat
import zipfile


DIRECTORIES = {'bgm', 'events', 'gfx', 'maps', 'materials', 'models', 'overviews',
               'resource', 'shader', 'sound', 'sprites'}
EXTENSIONS = {'.bsp', '.wad', '.mdl', '.seq', '.spr', '.wav', '.mp3', '.ogg',
              '.tga', '.bmp', '.dds', '.png', '.jpg', '.jpeg', '.pcx', '.lmp',
              '.ttf', '.otf', '.webm', '.res', '.txt', '.json', '.cso', '.csv',
              '.jsoo', '.sc', '.lst'}


def plain(path):
    path = Path(os.path.abspath(path))
    for part in [path, *path.parents]:
        if part.exists() and part.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Reparse point is not an archive source or destination')
    return path


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def selected(name):
    path = PurePosixPath(name)
    if len(path.parts) == 1 and path.name.lower() in {'cached.wad', 'tempdecal.wad', 'spraypaint.wad'}:
        return False
    return (path.suffix.lower() in EXTENSIONS and
            (len(path.parts) > 1 and path.parts[0].lower() in DIRECTORIES or
             len(path.parts) == 1 and path.suffix.lower() in {'.wad', '.lmp'}))


def build(source, output, evidence, runtime_list, client_plan, stable_closure):
    source, output, evidence = map(plain, (source, output, evidence))
    if source.name.lower() != 'cstrike' or output.is_relative_to(source):
        raise ValueError('Choose a cstrike resource source and a separate output')
    if output.exists() or evidence.exists():
        raise ValueError('Use new output and evidence paths')
    evidence.mkdir(parents=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    rows, keys = [], set()
    for folder, directories, files in os.walk(source, followlinks=False):
        folder = Path(folder)
        if folder == source:
            directories[:] = [name for name in directories if name.lower() in DIRECTORIES]
        for name in directories:
            plain(folder/name)
        for name in sorted(files):
            path = folder/name
            relative = path.relative_to(source).as_posix()
            if not selected(relative):
                continue
            plain(path)
            if relative.lower() in keys:
                raise ValueError('Case-colliding resource path')
            keys.add(relative.lower())
            before = path.stat()
            digest = sha(path)
            after = path.stat()
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                raise ValueError('Resource changed during inventory')
            rows.append(dict(path=relative, bytes=after.st_size, sha256=digest,
                             mtime_ns=after.st_mtime_ns, file_id=after.st_ino))
    rows.sort(key=lambda r: r['path'].lower())
    indexed = {row['path'].lower(): row for row in rows}
    sources, coverage = [], {}
    for file in [runtime_list, client_plan, stable_closure]:
        sources.append(dict(path=str(plain(file)), sha256=sha(plain(file))))
    runtime = json.loads(Path(runtime_list).read_text('utf-8-sig'))
    runtime_paths = sorted(set(name for paths in runtime.values() for name in paths))
    fallback = []
    for name in runtime_paths:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
            raise ValueError('Invalid required resource path')
        if name.lower() in indexed:
            continue
        if plain(source/name).is_file():
            raise ValueError('Client resource excluded from archive: '+name)
        base = plain(source.parent/'valve'/name)
        if not base.is_file():
            raise ValueError('Required runtime resource missing: '+name)
        fallback.append(name)
    coverage['runtimeCstrikeFiles'] = len(runtime_paths)-len(fallback)
    coverage['baseValveFilesNotBundled'] = fallback
    client = json.loads(Path(client_plan).read_text('utf-8-sig'))
    count = 0
    for row in client['files']:
        if not row.get('client'):
            continue
        actual = indexed.get(row['destination'].lower())
        if not actual or (actual['bytes'], actual['sha256']) != (row['bytes'], row['sha256']):
            raise ValueError('Deployed client resource differs from verified plan')
        count += 1
    coverage['currentClientPlanFiles'] = count
    stable = json.loads(Path(stable_closure).read_text('utf-8-sig'))
    verified, server_only = 0, []
    for row in stable['closure']['resources']:
        name = row['resource']
        if not selected(name):
            server_only.append(name)
            continue
        actual = indexed.get(name.lower())
        if not actual or (actual['bytes'], actual['sha256']) != (row['bytes'], row['sha256']):
            raise ValueError('Stable resource identity mismatch: '+name)
        verified += 1
    coverage['stableFilesVerified'] = verified
    coverage['stableServerOnlyNotBundled'] = server_only
    plan = dict(source=str(source), output=str(output), files=rows, sourceEvidence=sources,
                coverage=coverage, bytes=sum(r['bytes'] for r in rows))
    (evidence/'plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'phase':'inventory','files':len(rows),'bytes':plan['bytes'],
                      'runtimeCstrikeFiles':coverage['runtimeCstrikeFiles'],
                      'currentClientPlanFiles':count,'stableFilesVerified':verified}), flush=True)
    temporary = output.with_name(output.name+'.partial')
    if temporary.exists():
        raise ValueError('Unfinished archive already exists')
    with zipfile.ZipFile(temporary, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6,
                         allowZip64=True) as archive:
        for index, row in enumerate(rows, 1):
            path = plain(source/row['path'])
            before = path.stat()
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (row['bytes'], row['mtime_ns'], row['file_id']):
                raise ValueError('Source changed after inventory')
            name = 'cstrike_downloads/'+row['path']
            info = zipfile.ZipInfo.from_file(path, name)
            info.compress_type = zipfile.ZIP_DEFLATED
            info._compresslevel = 6
            digest, size = hashlib.sha256(), 0
            with path.open('rb') as stream, archive.open(info, 'w', force_zip64=True) as dest:
                while chunk := stream.read(1024*1024):
                    digest.update(chunk); size += len(chunk); dest.write(chunk)
            after = path.stat()
            if (digest.hexdigest() != row['sha256'] or size != row['bytes'] or
                    (after.st_size, after.st_mtime_ns, after.st_ino) != (row['bytes'], row['mtime_ns'], row['file_id'])):
                raise ValueError('Source changed during compression')
            if index % 500 == 0:
                print(json.dumps({'phase':'compress','files':index,'total':len(rows)}), flush=True)
    with zipfile.ZipFile(temporary) as archive:
        expected = {'cstrike_downloads/'+row['path']: row for row in rows}
        if len(archive.namelist()) != len(expected) or set(archive.namelist()) != set(expected):
            raise ValueError('ZIP path inventory mismatch')
        for index, (name, row) in enumerate(expected.items(), 1):
            with archive.open(name) as stream:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
            if digest != row['sha256'] or archive.getinfo(name).file_size != row['bytes']:
                raise ValueError('ZIP payload verification failed')
            if index % 1000 == 0:
                print(json.dumps({'phase':'verify','files':index,'total':len(rows)}), flush=True)
    for row in sources:
        if sha(Path(row['path'])) != row['sha256']:
            raise ValueError('Required resource evidence changed during packaging')
    os.replace(temporary, output)
    receipt = dict(archive=str(output), bytes=output.stat().st_size, sha256=sha(output),
                   fileCount=len(rows), uncompressedBytes=plan['bytes'], topDirectory='cstrike_downloads',
                   scriptsOrProgramsIncluded=False, allPayloadHashesVerified=True,
                   extractedStagingBytes=0, sourceModified=False, coverage=coverage)
    (evidence/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in receipt.items() if k!='coverage'}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['source','output','evidence','runtime-list','client-plan','stable-closure']:
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    build(args.source,args.output,args.evidence,args.runtime_list,args.client_plan,args.stable_closure)
