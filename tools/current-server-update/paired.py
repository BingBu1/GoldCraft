"""Stage or seal a narrow engine/launcher delta from explicitly approved inputs.

The deployed server is read-only. Unchanged resources and operator configuration
are neither copied nor rewritten; prior server and signed publication identities
are checked separately. No source snapshots or game executable are produced.
"""
from pathlib import Path
import argparse
import base64
import importlib.util
import json
import os
import shutil
import zipfile

spec = importlib.util.spec_from_file_location('current_prepare', Path(__file__).with_name('prepare.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
ROOT, plain, sha, identity = base.ROOT, base.plain, base.sha, base.identity


def release(path):
    return json.loads(base64.b64decode(json.loads(path.read_text('utf-8-sig'))['payload']))


def stage(publication, baseline, client_plan, server_review, output):
    publication, baseline, client_plan, server_review, output = map(
        plain, (publication, baseline, client_plan, server_review, output))
    if output.exists() or not output.is_relative_to(ROOT/'server-packages'):
        raise ValueError('Use a fresh server-packages output')
    new, old = release(publication/'manifest.json'), release(baseline/'manifest.json')
    audited = json.loads(client_plan.read_text('utf-8'))
    if new != audited['release'] or old['sequence'] >= new['sequence'] or old['bundleId'] != new['bundleId']:
        raise ValueError('Publication differs from reviewed inputs')
    for row in audited['auditFiles']:
        if (sha(plain(row['source'])), Path(row['source']).stat().st_size) != (row['sha256'], row['bytes']):
            raise ValueError('Reviewed client input changed')
    review = json.loads(server_review.read_text('utf-8'))
    server = {r['path']: r for r in review['files']}
    if [r['path'] for r in review['files'] if r.get('changed')] != ['cs/swds.dll']:
        raise ValueError('This narrow release permits only the reviewed engine change')
    files, required = [], []
    for name in ('cs/swds.dll', 'cs/filesystem_stdio.dll', 'cs/cstrike/dlls/mp.dll'):
        row = server[name]
        actual = identity(row['source'])
        if (actual['bytes'], actual['sha256']) != (row['bytes'], row['sha256']):
            raise ValueError('Reviewed server component changed')
        base.x86(actual['source'])
        (files if row.get('changed') else required).append(dict(path=name, **actual, priorSha256=row['priorSha256']))
    previous = {r['path']: r for r in old['files']}
    changed_cores = []
    for row in new['files']:
        rel = 'cs/launcher-public/blobs/'+row['sha256']+'/'+Path(row['path']).name
        source = publication/rel.removeprefix('cs/launcher-public/')
        actual = identity(source)
        if (actual['sha256'], actual['bytes']) != (row['sha256'], row['bytes']):
            raise ValueError('Publication DLL differs from contract')
        entry = dict(path=rel, **actual, priorSha256=[])
        if previous.get(row['path']) == row:
            required.append(entry)
        else:
            files.append(entry)
            changed_cores.append(row['path'])
    files.append(dict(path='cs/launcher-public/manifest.json', **identity(publication/'manifest.json'),
                      priorSha256=[sha(baseline/'manifest.json')]))
    manifest = dict(schema=1, scope='paired-managed-delta', sequence=new['sequence'], baselineSequence=old['sequence'],
                    serverBaseline='20261107-Deployed',
                    files=[{k:r[k] for k in ('path','bytes','sha256','priorSha256')} for r in files],
                    requiredBaseline=[{k:r[k] for k in ('path','bytes','sha256','priorSha256')} for r in required])
    package = output/('BingSang-Server-Update-'+str(new['sequence']))
    package.mkdir(parents=True)
    sources = {
        'Update-Server.ps1': ROOT/'tools/current-server-update/Paired-Update.ps1',
        'Paired-Publication.ps1': ROOT/'tools/current-server-update/Paired-Publication.ps1',
        'Files.ps1': ROOT/'tools/scenario-update/Files.ps1',
        'release-public-key.xml': ROOT/'tools/respawn-release/release-public-key.xml',
        'baseline-manifest.json': baseline/'manifest.json',
    }
    sources.update({'payload/'+r['path']: Path(r['source']) for r in files})
    for name, source in sources.items():
        dest = plain(package/name);dest.parent.mkdir(parents=True, exist_ok=True)
        if name.endswith('.ps1'):
            dest.write_text(source.read_text('utf-8-sig'), 'utf-8-sig', newline='\r\n')
        else:
            shutil.copy2(plain(source), dest)
            if sha(dest) != sha(source):
                raise ValueError('Staged copy changed')
        if dest.stat().st_nlink != 1:
            raise ValueError('Mutable staged file must be independent')
    (package/'update-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
    for name, flag in [('Update-Server.cmd',''), ('Restore-Server.cmd','-Restore ')]:
        (package/name).write_bytes(('@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-Server.ps1" '+flag+
                                   '%*\r\nset "result=%ERRORLEVEL%"\r\npause\r\nexit /b %result%\r\n').encode('ascii'))
    (package/'README.txt').write_text(
        '冰桑服务器联合增量更新 '+str(new['sequence'])+'\n\n'
        '适用：已安装 20261107 累计服务器包，以及启动器服务端发布 '+str(old['sequence'])+'。\n'
        '本包包含服务端引擎更新，必须停服后安装并重启。\n\n'
        '1. 解压到服务器目录之外。\n'
        '2. 用原 Stop.cmd 停服；关闭服务器和自动重启监护进程。\n'
        '3. 双击 Update-Server.cmd，默认目标 E:\\cs_MineCraft\\GoldCraft-Servers。\n'
        '4. 显示 Updated 或 Already current 后，用原 Start.cmd 启动。\n'
        '5. 玩家退出 CS，继续使用 2026.11.08.2 或更新的冰桑启动器，点击启动游戏获取更新。无需换 EXE。\n\n'
        '更换安装目录：Update-Server.cmd -TargetDirectory "D:\\GoldCraft-Servers"\n'
        '只读预检：Update-Server.cmd -Plan（可附同样的目录参数）。\n'
        '恢复：停服后执行 Restore-Server.cmd，再用原 Start.cmd 启动。\n'
        '已更新的客户端会拒绝回退到旧发布；回滚用于服务端恢复，后续需更高序号的配套修复发布。\n\n'
        '包含 1 个服务端 DLL、'+str(len(changed_cores))+' 个变动客户端 DLL，以及完整签名目录/shader 清单。\n'
        '其余 '+str(len(new['files'])-len(changed_cores))+' 个客户端 DLL 逐 SHA 验证后复用。缺少基线时更新前停止。\n'
        '端口、密码、管理员、模式、原启动脚本、Minecraft 和地图资源保持原样。\n'
        '安装自动备份；中断可重新运行同一脚本恢复。不要手工覆盖 payload，以免漏掉签名清单和依赖检查。\n', 'utf-8-sig')
    receipt = dict(package=str(package), sequence=new['sequence'], baselineSequence=old['sequence'], files=files,
                   requiredBaseline=required, changedClientDLLs=changed_cores, resources=len(new['resources']),
                   inputs=[identity(p) for p in [client_plan,server_review,publication/'manifest.json',baseline/'manifest.json']],
                   scriptInputs=[identity(p) for p in sources.values() if p.suffix=='.ps1'], sealed=False)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', 'utf-8')
    print(json.dumps(dict(package=str(package),payloadFiles=len(files),baselineFiles=len(required),sealed=False)))


def seal(output, server_test, client_test):
    output=plain(output)
    if not output.is_relative_to(ROOT/'server-packages'):
        raise ValueError('Output must stay inside server-packages')
    receipt_path=output/'receipt.json';receipt=json.loads(receipt_path.read_text('utf-8'))
    package=plain(receipt['package'])
    if package.parent != output or receipt['sealed']:
        raise ValueError('Unexpected or previously sealed stage')
    server_test, client_test=map(plain,(server_test,client_test))
    server_result=json.loads(server_test.read_text('utf-8'))
    client_result=json.loads(client_test.read_text('utf-8'))
    if not server_result['passed'] or not client_result['passed'] or client_result['sequence']!=receipt['sequence']:
        raise ValueError('Matching successful installer and client upgrade receipts are required')
    for name,digest in server_result['inputHashes'].items():
        if sha(plain(package/name))!=digest:
            raise ValueError('Tested installer changed before sealing')
    if sha(package/'payload/cs/launcher-public/manifest.json')!=client_result['manifestSha256']:
        raise ValueError('Tested signed publication changed')
    for row in receipt['inputs']+receipt['scriptInputs']:
        if sha(plain(row['source'])) != row['sha256']:
            raise ValueError('Reviewed input changed before sealing')
    for row in receipt['files']:
        if sha(plain(row['source'])) != row['sha256'] or sha(plain(package/'payload'/row['path'])) != row['sha256']:
            raise ValueError('Payload changed before sealing')
    for row in receipt['scriptInputs']:
        source=plain(row['source'])
        name='Update-Server.ps1' if source.name=='Paired-Update.ps1' else source.name
        if (package/name).read_text('utf-8-sig')!=source.read_text('utf-8-sig'):
            raise ValueError('Staged script differs from reviewed source')
    rows=[dict(path=p.relative_to(package).as_posix(),**identity(p)) for p in sorted(package.rglob('*')) if p.is_file()]
    (package/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['path']+'\n' for r in rows),'utf-8')
    rows.append(dict(path='SHA256SUMS.txt',**identity(package/'SHA256SUMS.txt')))
    archive=output/(package.name+'.zip');temp=archive.with_suffix('.zip.partial')
    with zipfile.ZipFile(temp,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for row in rows:z.write(package/row['path'],package.name+'/'+row['path'])
    import hashlib
    with zipfile.ZipFile(temp) as z:
        if len(z.infolist()) != len(rows):raise ValueError('ZIP inventory mismatch')
        for row in rows:
            with z.open(package.name+'/'+row['path']) as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
            if digest != row['sha256'] or sha(package/row['path']) != row['sha256']:
                raise ValueError('ZIP or staged source changed')
    os.replace(temp,archive)
    receipt.update(sealed=True,archive=identity(archive),members=rows,allZipHashesVerified=True,
                   validations=[identity(server_test),identity(client_test)])
    receipt_path.write_text(json.dumps(receipt,indent=2)+'\n','utf-8')
    print(json.dumps(receipt['archive']))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    s=sub.add_parser('stage')
    for name in ('publication','baseline','client-plan','server-review','output'):s.add_argument('--'+name,type=Path,required=True)
    s=sub.add_parser('seal')
    for name in ('output','server-test','client-test'):s.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    if a.command=='stage':stage(a.publication,a.baseline,a.client_plan,a.server_review,a.output)
    else:seal(a.output,a.server_test,a.client_test)
