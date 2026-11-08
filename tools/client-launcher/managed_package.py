"""Package every managed release blob and the reusable server publication updater."""
from pathlib import Path
import argparse
import base64
import importlib.util
import json
import shutil
import zipfile
import hashlib

spec=importlib.util.spec_from_file_location('launcher_package',Path(__file__).with_name('package.py'))
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)


def package(publication,output):
    publication,output=map(base.plain,(publication,output));root=base.ROOT
    if output.exists() or not output.is_relative_to(root/'server-packages'):raise ValueError('Use a fresh server-packages directory')
    envelope=json.loads((publication/'manifest.json').read_text('utf-8-sig'));release=json.loads(base64.b64decode(envelope['payload']))
    if release['schema']!=2:raise ValueError('Requires a managed release')
    stage=output/('BingSang-Launcher-Server-'+str(release['sequence']));stage.mkdir(parents=True)
    sources={'Update-Server.ps1':Path(__file__).with_name('Update-Server.ps1'),
             'Files.ps1':root/'tools/scenario-update/Files.ps1','release-public-key.xml':root/'tools/respawn-release/release-public-key.xml',
             'payload/cs/launcher-public/manifest.json':publication/'manifest.json'}
    for row in release['files']:
        rel='blobs/'+row['sha256']+'/'+Path(row['path']).name;p=base.plain(publication/rel)
        if base.digest(p)!=row['sha256'] or p.stat().st_size!=row['bytes']:raise ValueError('Core blob mismatch')
        sources['payload/cs/launcher-public/'+rel]=p
    for rel,source in sources.items():
        target=base.plain(stage/rel);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base.plain(source),target)
        if base.digest(target)!=base.digest(source):raise ValueError('Package copy failed')
    for name,flag in [('Update-Server.cmd',''),('Restore-Server.cmd','-Restore ')]:
        (stage/name).write_bytes(('@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-Server.ps1" '+flag+'%*\r\nset "result=%ERRORLEVEL%"\r\npause\r\nexit /b %result%\r\n').encode('ascii'))
    (stage/'README.txt').write_text('冰桑启动器自动更新服务端包 '+release['version']+'\n\n'
        '解压到服务器目录之外，双击 Update-Server.cmd。默认目录 E:\\cs_MineCraft\\GoldCraft-Servers。\n'
        '完整包含 '+str(len(release['files']))+' 个核心 DLL 及 '+str(len(release['resources']))+' 个签名符号目录/shader 配套文件。\n'
        '无需重启服务器，不修改端口、密码、管理员和其他游戏配置。\n'
        '玩家本次换用 2026.11.08.2 或更新的冰桑启动器，今后本协议下 DLL/符号目录/shader 更新只需点击启动游戏。\n'
        '旧 2026.11.08.1 及更早 EXE 不支持此协议。地图模型资源仍由原下载渠道管理。\n'
        '其他目录：Update-Server.cmd -TargetDirectory "D:\\GoldCraft-Servers"\n'
        '仅检查：Update-Server.cmd -Plan；恢复清单：Restore-Server.cmd（可附同样目录参数）。\n'
        '恢复旧清单后，新版客户端会拒绝回退；修复发布应使用更高序号，不能让玩家继续混用不配套文件。\n','utf-8-sig')
    files=[dict(path=p.relative_to(stage).as_posix(),bytes=p.stat().st_size,sha256=base.digest(p)) for p in sorted(stage.rglob('*')) if p.is_file()]
    archive=output/(stage.name+'.zip')
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for row in files:z.write(stage/row['path'],stage.name+'/'+row['path'])
    with zipfile.ZipFile(archive) as z:
        if len(z.infolist())!=len(files):raise ValueError('Unexpected ZIP member')
        for row in files:
            with z.open(stage.name+'/'+row['path']) as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
            if digest!=row['sha256']:raise ValueError('ZIP hash mismatch')
    result=dict(sequence=release['sequence'],bundleId=release['bundleId'],coreFiles=len(release['files']),resources=len(release['resources']),files=files,
                zip=dict(path=str(archive),bytes=archive.stat().st_size,sha256=base.digest(archive)))
    (output/'receipt.json').write_text(json.dumps(result,indent=2)+'\n','utf-8');print(json.dumps(result['zip']))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--publication',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();package(a.publication,a.output)
