#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ship the six omitted guest scripts over the immutable Nightfall Hotfix1."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RELEASE = '20261115-Nightfall-GuestBootstrap'
SCRIPTS = ['aurora/guest/cof/gunsmoke.aur'] + [
    'inventoryitems/guest/cof/weapons/weapon_' + name + '.txt'
    for name in ('glock', 'p345', 'revolver', 'vp70', 'tmp')]


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def checked(path):
    path = path.absolute()
    if not path.resolve().is_relative_to(ROOT) or any(p.is_symlink() or p.is_junction() for p in (path, *path.parents)):
        raise ValueError('Use independent workspace paths')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'resources', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    baseline, resources, output = map(checked, (args.baseline, args.resources, args.output))
    if output.exists():
        raise ValueError('Use a fresh output; issued packages are immutable')
    old = json.loads((baseline / 'update-manifest.json').read_text('utf-8-sig'))
    if old['release'] != '20261115-Nightfall-Hotfix1' or old['managedSequence'] != 2026111502:
        raise ValueError('Requires the issued Nightfall Hotfix1 baseline')
    indexed = {row['path']: row for row in old['files'] + old['requiredBaseline']}
    if len(indexed) != len(old['files']) + len(old['requiredBaseline']):
        raise ValueError('Duplicate baseline member')
    available = {row['destination']: row for row in json.loads(resources.read_text('utf-8-sig'))['files']}
    package = output / ('GoldCraft-Server-Update-' + RELEASE)
    package.mkdir(parents=True)
    files, inputs = [], []
    for name in SCRIPTS:
        row = available[name]
        source = checked(Path(row['source']))
        if source.stat().st_size != row['bytes'] or sha(source) != row['sha256'] or not row['bytes']:
            raise ValueError('Script source changed: ' + name)
        target = 'cs/cstrike/' + name
        if target in indexed:
            raise ValueError('Expected omitted guest script is already in the baseline')
        dest = package / 'payload' / target
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        if sha(dest) != row['sha256']:
            raise ValueError('Staging identity changed')
        files.append(dict(path=target, bytes=row['bytes'], sha256=row['sha256'], priorSha256=[]))
        inputs.append(dict(path=str(source), bytes=row['bytes'], sha256=row['sha256']))
    manifest = dict(schema=1, release=RELEASE, scope=old['scope'],
        sourceRevision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, encoding='ascii').strip(),
        files=files, requiredBaseline=copy.deepcopy(list(indexed.values())),
        requiredPlugins=old['requiredPlugins'], managedSequence=old['managedSequence'], bundleId=old['bundleId'])
    (package / 'update-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    for source in baseline.iterdir():
        if source.is_file() and source.name not in ('update-manifest.json', 'README.md'):
            shutil.copy2(source, package / source.name)
    updater = package / 'Update-Server.ps1'
    text = updater.read_text('utf-8-sig')
    if old['release'] not in text or '$script:configuration=@{}' not in text:
        raise ValueError('Unexpected baseline repair installer')
    text = text.replace(old['release'], RELEASE)
    marker = 'function Payload-Path([string]$Name){'
    if text.count(marker) != 1:
        raise ValueError('Unexpected payload scope')
    text = text.replace(marker, marker + "\n    if($Name -cmatch '^cs/cstrike/(?:aurora/guest/cof/[A-Za-z0-9_/-]+\\.aur|inventoryitems/guest/cof/[A-Za-z0-9_/-]+\\.txt)$'){return $true}")
    updater.write_text(text, encoding='utf-8-sig')
    publication = package / 'Publication.ps1'
    text = publication.read_text('utf-8-sig')
    before = "Read-NightfallPublication (Under $Package 'payload/cs/launcher-public/manifest.json')"
    if text.count(before) != 1:
        raise ValueError('Unexpected signed baseline verification')
    # No client binary/catalog changed. Verify the already-installed signed
    # publication, which is itself required by SHA in the immutable baseline.
    text = text.replace(before, "Read-NightfallPublication (Under $Target 'cs/launcher-public/manifest.json')")
    publication.write_text(text, encoding='utf-8-sig')
    (package / 'README.md').write_text(
        '# 客串武器资源补丁\n\n'
        '适用已安装 20261115-Nightfall-Hotfix1 的服务器。默认路径 '
        '`E:\\cs_MineCraft\\GoldCraft-Servers`。\n\n'
        '停服，将本目录放在服务器目录之外，双击 Update-Server.cmd，成功后使用原 Start.cmd 启动。'
        '重新进入地图后，缺少的六个资源会通过现有 HTTP 下载自动提供给玩家。无需手工安装客户端修复包。\n\n'
        '修复更新包漏发枪烟脚本和五份武器库存脚本，导致整组客串武器无法注册／购买的问题。'
        '六个文件之外的基线全部逐 SHA 验证；不重置服主设置，不替换 DLL 或重复发布客户端。'
        '重复执行幂等；Restore-Server.cmd 恢复本次变化。\n\n'
        '满体力连跳预测问题仍独立排查，本资源补丁不宣称修复跳跃。\n', encoding='utf-8')
    for name in ('Inspect-Runtime.ps1', 'Inspect-ScenarioState.ps1'):
        source = ROOT / 'tools/nightfall-hotfix' / name
        shutil.copy2(source, package / name)
    archive = output / (package.name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                z.write(path, package.name + '/' + path.relative_to(package).as_posix())
    with zipfile.ZipFile(archive) as z:
        for path in package.rglob('*'):
            if path.is_file() and hashlib.sha256(z.read(package.name + '/' + path.relative_to(package).as_posix())).hexdigest() != sha(path):
                raise ValueError('Archive differs from staged input')
    result = dict(package=str(package), archive=str(archive), bytes=archive.stat().st_size, sha256=sha(archive),
                  files=len(files), baseline=len(indexed), inputs=inputs, remoteDeployed=False, clientDLLChanged=False)
    (output / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    (output / 'SHA256SUMS.txt').write_text(result['sha256'] + '  ' + archive.name + '\n', encoding='utf-8')
    print(json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
