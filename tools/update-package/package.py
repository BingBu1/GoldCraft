"""Build cumulative server/player updates from a verified deployed candidate.

Only explicit runtime/plugin resources are included. No credentials, settings,
worlds, game engine/client DLLs or unrelated Minecraft mods are distributed.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,zipfile

R=Path(__file__).resolve().parents[2]

def load_module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

client=load_module('client_package',R/'tools/client-installer/package.py')

def sha(data):return hashlib.sha256(data).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))

def build(candidate,output,revision):
    candidate=client.safe(candidate);output=client.safe(output)
    if output.exists():raise ValueError('Use a new output folder')
    frozen=read(candidate/'manifest.json')
    if not frozen['deployed']:raise ValueError('Candidate is not deployed')
    sources={}
    for row in frozen['artifacts']:
        path=client.safe(R/row['path']);data=path.read_bytes()
        if not path.resolve().is_relative_to(candidate.resolve()) or sha(data)!=row['sha256']:raise ValueError('Frozen candidate changed')
        sources[path.relative_to(candidate).as_posix()]=path
    core=sources['goldcraft-neoforge-0.1.0-dev.jar']
    with zipfile.ZipFile(core) as z:
        caps=json.loads(z.read('META-INF/goldcraft-capabilities.json'))
        assert caps['csAdmissionVersion']==1 and caps['offlineCsAdmission']
    game=R/'sandbox/cs-client-b/Half-Life';server=R/'sandbox/cs-server/Half-Life'
    deployed={'GoldCraft.dll':game/'cstrike/metahook/plugins/GoldCraft.dll',
        'mp.dll':server/'cstrike/dlls/mp.dll','server/swds.dll':server/'swds.dll',
        'server/goldcraft_amxx.dll':server/'cstrike/addons/amxmodx/modules/goldcraft_amxx.dll',
        'goldcraft-neoforge-0.1.0-dev.jar':R/'sandbox/neoforge-server/mods'/core.name}
    for name,path in sources.items():
        if name.startswith('client/'):
            relative=name.removeprefix('client/')
            deployed[name]=game/('cstrike/'+relative if relative.startswith('renderer/') else 'cstrike/metahook/plugins/'+relative)
        elif name.startswith('gamedata/'):
            deployed[name]=game/'cstrike/metahook'/name
        elif name.startswith('server/plugins/'):
            deployed[name]=server/'cstrike/addons/amxmodx/plugins'/Path(name).name
    for name,path in deployed.items():
        if sha(client.safe(path).read_bytes())!=sha(sources[name].read_bytes()):raise ValueError('Deployed file differs from candidate: '+name)
    output.mkdir(parents=True)
    release='20261016-TNT2-Support'
    receipts=[]
    for kind in ('Server','Players'):
        root=output/('GoldCraft-'+kind+'-Update-'+release);root.mkdir()
        files=[];names=set();roles=['server'] if kind=='Server' else ['cs','minecraft']
        def add(role,destination,source):
            name='payload/'+role+'/'+client.relative_name(destination)
            if name.casefold() in names:raise ValueError('Duplicate payload '+name)
            names.add(name.casefold())
            data=source if isinstance(source,bytes) else client.safe(source).read_bytes()
            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            files.append(dict(path=name,role=role,destination=destination,bytes=len(data),sha256=sha(data)))
        if kind=='Server':
            add('server','cs/swds.dll',sources['server/swds.dll'])
            add('server','cs/cstrike/dlls/mp.dll',sources['mp.dll'])
            add('server','cs/cstrike/addons/amxmodx/modules/goldcraft_amxx.dll',sources['server/goldcraft_amxx.dll'])
            amxx=server/'cstrike/addons/amxmodx'
            plugins=set()
            for listing in [amxx/'configs/plugins.ini',*sorted((amxx/'configs').glob('plugins-*.ini'))]:
                for line in listing.read_text(encoding='utf-8-sig').splitlines():
                    fields=line.split(';',1)[0].strip().split()
                    if not fields or 'disabled' in fields[1:]:continue
                    name=fields[0]
                    if not re.fullmatch(r'[\w-]+\.amxx',name) or 'test' in name or 'fixture' in name:raise ValueError('Unexpected runtime plugin '+name)
                    plugins.add(name)
            for name in sorted(plugins):add('server','cs/cstrike/addons/amxmodx/plugins/'+name,amxx/'plugins'/name)
            for path in sorted((amxx/'data/lang').glob('*.txt')):add('server','cs/cstrike/addons/amxmodx/data/lang/'+path.name,path)
            for path in sorted((R/'tools/server-package').rglob('*')):
                if path.is_file() and path.name!='server-settings.json':add('server',path.relative_to(R/'tools/server-package').as_posix(),path)
            for name in ('scope_arc.tga','scope_arc_ne.tga','scope_arc_nw.tga','scope_arc_sw.tga'):
                add('server','cs/cstrike/sprites/'+name,server/'cstrike/sprites'/name)
            add('server','minecraft/mods/'+core.name,core)
            add('server','client-update/mods/'+core.name,core)
            requirements=dict(minecraft='1.21.1',neoforge='21.1.256',wireProtocol=30,nativeMessageCount=90,
                goldcraftCoreSha256=sha(core.read_bytes()),pairedClientRelease=release,
                mods=sorted(p.name for p in (R/'sandbox/neoforge-server/mods').glob('*.jar')))
            add('server','client-requirements.json',(json.dumps(requirements,indent=2)+'\n').encode())
            instructions='''服务器累计补丁（2026-10-16）

1. 在服务器原目录运行 Stop.cmd，等待 CS、MC 都正常退出。
2. 本补丁完整解压到一个新的目录，不要直接覆盖到服务端。
3. 双击 Update-Server.cmd，输入原 Start.cmd 所在目录。
4. 显示 Updated server 后，用原目录 Start.cmd 启动。

保留 server-settings.json、公网端口、RCON/管理员、其他配置、.state、存档及其他 Mod。
只将 mc_map_mining=2、mc_round_clear_blocks=1、mc_death_drop_items=0 写入现有 server.cfg。
清块/还原发生在下一回合开始。会去重并启用 GoldCraft/F6/僵尸规则插件，备份实际变更至 .goldcraft-updates。
所有当前启用的 AMXX 二进制和翻译随补丁更新，保留用户自己的插件列表与无关插件。
不必重新配置公网端口。原 package-manifest.json 保留原包基线，本次文件以 update-manifest.json 和安装备份记录为准。
请将配套 Players ZIP 发给玩家；服务端更新不能自动替换玩家的 CS 插件与 MC Mod。
'''
        else:
            for name in ('MetaHook.exe','SDL2.dll','SDL3.dll'):add('cs',name,game/name)
            for name in client.PLUGINS:add('cs','cstrike/metahook/plugins/'+name,game/'cstrike/metahook/plugins'/name)
            add('cs','cstrike/metahook/plugins/GoldCraftSteamWorker.exe',sources['client/GoldCraftSteamWorker.exe'])
            add('cs','cstrike/metahook/plugins/BulletPhysics_AVX2.dll',game/'cstrike/metahook/plugins/BulletPhysics.dll')
            for tree,extensions in client.TREES.items():
                for path in sorted((game/'cstrike'/tree).rglob('*')):
                    client.safe(path)
                    if not path.is_file() or path.suffix.lower()=='.pdb' or (tree=='renderer/shader' and path.name.endswith('_cache.txt')):continue
                    if path.suffix.lower() not in extensions:raise ValueError('Review resource type '+str(path))
                    add('cs','cstrike/'+path.relative_to(game/'cstrike').as_posix(),path)
            for name in client.RESOURCE_FILES:add('cs','cstrike/'+name,game/'cstrike'/name)
            add('minecraft','mods/'+core.name,core)
            instructions='''普通玩家累计补丁（2026-10-16，已安装 GoldCraft 的玩家）

完整解压到新的目录。CS 与 MC 两部分都需要更新：
1. 退出 CS，双击 Update-CS.cmd，输入自己的 CS 根目录（有 hl.exe、hw.dll、cstrike 的文件夹）。
2. 退出 Minecraft，在 XMCL 中打开正在使用的 1.21.1 / NeoForge 21.1.256 实例目录。
3. 双击 Update-Minecraft.cmd，输入该实例目录（包含 mods 的那一层，不是 mods 文件夹）。
4. 用原来的 MetaHook for Counter-Strike 快捷方式启动 CS，先进入对应 CS 服务器，再启动 MC 连接同一组 MC 服务器。

本包更新整套配套 MetaHook 插件/资源与一个 GoldCraft 核心，不包含 CS 游戏本体或完整 MC 整合包。
保留账号、存档、其他 Mod（包括 FerriteCore）、按键与用户配置；自动备份被替换的文件。
不同文件名的旧 GoldCraft 会按 JAR 内真实类识别并移出 mods，避免重复加载。
CS 若提示引擎/client.dll 不匹配，停止更新并把提示发给服主；不要覆盖 hw.dll 或跳过检查。
原包的自带启动器、Steam 登录与验证保持。备份在各目标目录的 .goldcraft-updates。
'''
        common='''
本次：TNT碰撞后台准备/同步调度/卡顿时保持所选MC形态；沙子、铁砧等重力方块和墙/地面火把支撑。
同时带入此前输入中文/Shift/小键盘、流体/药水/盔甲、死亡观战、回合开始清理、F6和ZP规则等配套修复。
Epic/次元斩绝无敌新机制尚未加入。TNT仍有已知准备停顿，不能把此包当作全部卡顿已解决。
自动验证和本机单B启动配对通过；公网部署与其他玩家完整实测仍待。
'''
        (root/'先读我.txt').write_text(instructions+common,encoding='utf-8-sig')
        (root/'Update-GoldCraft.ps1').write_bytes((R/'tools/update-package/Update-GoldCraft.ps1').read_bytes())
        (root/'Common.ps1').write_bytes((R/'tools/client-installer/Common.ps1').read_bytes())
        for role in roles:
            name={'server':'Server','cs':'CS','minecraft':'Minecraft'}[role]
            cmd=f'@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-GoldCraft.ps1" -Role {role} %*\r\nif errorlevel 1 echo Update failed. Keep this window and report the error.\r\npause\r\n'
            (root/f'Update-{name}.cmd').write_bytes(cmd.encode('ascii'))
        manifest=dict(schema=1,release=release,sourceRevision=revision,roles=roles,files=files,
            supportedGame=dict(engineSha256=sha((game/'hw.dll').read_bytes()),clientSha256=sha((game/'cstrike/cl_dlls/client.dll').read_bytes())),
            plugins=list(client.PLUGINS),requiredPlugins=['goldcraft.amxx','goldcraft_controls.amxx','goldcraft_connection.amxx','goldcraft_zp_rules.amxx','goldcraft_zp_combat.amxx'])
        (root/'update-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        for path in (R/'notices').glob('*.txt'):
            destination=root/'licenses'/path.name;destination.parent.mkdir(exist_ok=True);destination.write_bytes(path.read_bytes())
        (root/'licenses/THIRD_PARTY_NOTICES.md').write_bytes((R/'THIRD_PARTY_NOTICES.md').read_bytes())
        archive=root.with_suffix('.zip')
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for path in sorted(root.rglob('*')):
                if path.is_file():z.write(path,path.relative_to(root).as_posix())
        with zipfile.ZipFile(archive) as z:
            if z.testzip():raise ValueError('ZIP CRC failure')
            for row in files:
                data=z.read(row['path'])
                if len(data)!=row['bytes'] or sha(data)!=row['sha256']:raise ValueError('ZIP payload mismatch')
            actual={n for n in z.namelist() if n.startswith('payload/')}
            assert actual=={r['path'] for r in files}
        receipts.append(dict(archive=str(archive),bytes=archive.stat().st_size,sha256=sha(archive.read_bytes()),payloadFiles=len(files)))
    (output/'SHA256SUMS.txt').write_text(''.join(f"{r['sha256']}  {Path(r['archive']).name}\n" for r in receipts),encoding='ascii')
    (output/'build-receipt.json').write_text(json.dumps(dict(revision=revision,packages=receipts),indent=2)+'\n')
    print(json.dumps(receipts))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--revision',required=True);args=parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}',args.revision):raise ValueError('Use full source commit')
    build(args.candidate,args.output,args.revision)
