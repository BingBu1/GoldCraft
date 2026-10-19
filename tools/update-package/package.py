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

def build(candidate,output,revision,release='20261019',server_only=False):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,63}',release):
        raise ValueError('Invalid release name')
    candidate=client.safe(candidate);output=client.safe(output)
    if output.exists():raise ValueError('Use a new output folder')
    frozen=read(candidate/'manifest.json')
    if not frozen['deployed']:raise ValueError('Candidate is not deployed')
    if frozen['publishedSourceRevision']!=revision:raise ValueError('Runtime source revision differs from freeze')
    protocol=frozen['protocol']
    if any(type(protocol[k]) is not int or protocol[k]<=0 for k in ('version','nativeMessageCount')):
        raise ValueError('Missing verified protocol metadata')
    sources={};runtime_resources={}
    for row in frozen['artifacts']:
        path=client.safe(R/row['path']);data=path.read_bytes()
        if not path.resolve().is_relative_to(candidate.resolve()) or sha(data)!=row['sha256']:raise ValueError('Frozen candidate changed')
        name=path.relative_to(candidate).as_posix()
        if name in sources:raise ValueError('Duplicate candidate artifact '+name)
        sources[name]=path
        if name.startswith('server/runtime/'):
            relative=client.relative_name(row['runtimeRelative'])
            if relative!='cstrike/'+name.removeprefix('server/runtime/') or not re.fullmatch(
                r'cstrike/(?:addons/amxmodx/(?:plugins/[\w-]+\.amxx|data/lang/[\w-]+\.txt|configs/plugins-[\w-]+\.ini)|models/[\w/-]+\.mdl|sprites/zp_[\w-]+\.spr)',relative):
                raise ValueError('Unsupported server runtime resource '+relative)
            runtime_resources[name]=relative
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
        if name.startswith('client/runtime/'):
            relative=client.relative_name(name.removeprefix('client/runtime/'))
            if not re.fullmatch(r'sprites/zp_[\w-]+\.spr',relative):raise ValueError('Unsupported client runtime resource '+relative)
            deployed[name]=game/'cstrike'/relative
        elif name.startswith('client/'):
            relative=name.removeprefix('client/')
            deployed[name]=game/('cstrike/'+relative if relative.startswith('renderer/') else 'cstrike/metahook/plugins/'+relative)
        elif name.startswith('gamedata/'):
            deployed[name]=game/'cstrike/metahook'/name
        elif name.startswith('server/plugins/'):
            deployed[name]=server/'cstrike/addons/amxmodx/plugins'/Path(name).name
        elif name.startswith('server/configs/'):
            deployed[name]=server/'cstrike/addons/amxmodx/configs'/Path(name).name
        elif name in runtime_resources:
            deployed[name]=server/runtime_resources[name]
    if set(sources)!=set(deployed):raise ValueError('Unmapped candidate artifacts: '+str(set(sources)-set(deployed)))
    for name,path in deployed.items():
        if sha(client.safe(path).read_bytes())!=sha(sources[name].read_bytes()):raise ValueError('Deployed file differs from candidate: '+name)
    output.mkdir(parents=True)
    receipts=[]
    for kind in (('Server',) if server_only else ('Server','Players')):
        root=output/('GoldCraft-'+kind+'-Update-'+release);root.mkdir()
        files=[];names={};roles=['server'] if kind=='Server' else ['cs','minecraft']
        required_plugins=['goldcraft.amxx','goldcraft_controls.amxx','goldcraft_connection.amxx','goldcraft_zp_rules.amxx','goldcraft_zp_combat.amxx']
        for plugin in ('zp50_zombie_armor.amxx','zp50_zombie_respawn.amxx','zp50_zombie_knockback.amxx','zp50_zombie_gore.amxx'):
            if 'server/plugins/'+plugin in sources:required_plugins.append(plugin)
        def add(role,destination,source,*,allow_identical=False):
            name='payload/'+role+'/'+client.relative_name(destination)
            data=source if isinstance(source,bytes) else client.safe(source).read_bytes()
            if name.casefold() in names:
                if allow_identical and names[name.casefold()]==sha(data):return
                raise ValueError('Duplicate or conflicting payload '+name)
            names[name.casefold()]=sha(data)
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
            for name,source in sorted(sources.items()):
                if name.startswith('server/configs/'):
                    add('server','cs/cstrike/addons/amxmodx/configs/'+Path(name).name,source)
            for path in sorted((amxx/'data/lang').glob('*.txt')):add('server','cs/cstrike/addons/amxmodx/data/lang/'+path.name,path)
            for name,relative in sorted(runtime_resources.items()):
                add('server','cs/'+relative,sources[name],allow_identical=True)
                if relative.startswith('cstrike/addons/amxmodx/plugins/'):
                    plugin=Path(relative).name
                    if plugin not in required_plugins:required_plugins.append(plugin)
            for path in sorted((R/'tools/server-package').rglob('*')):
                if path.is_file() and path.name!='server-settings.json':add('server',path.relative_to(R/'tools/server-package').as_posix(),path)
            for name in ('scope_arc.tga','scope_arc_ne.tga','scope_arc_nw.tga','scope_arc_sw.tga'):
                add('server','cs/cstrike/sprites/'+name,server/'cstrike/sprites'/name)
            add('server','minecraft/mods/'+core.name,core)
            add('server','client-update/mods/'+core.name,core)
            requirements=dict(minecraft='1.21.1',neoforge='21.1.256',wireProtocol=protocol['version'],nativeMessageCount=protocol['nativeMessageCount'],
                amxx='1.10.0.5487',userinfoCapacity=1024,
                goldcraftCoreSha256=sha(core.read_bytes()),pairedClientRelease=frozen.get('pairedClientRelease',release),
                mods=sorted(p.name for p in (R/'sandbox/neoforge-server/mods').glob('*.jar')))
            add('server','client-requirements.json',(json.dumps(requirements,indent=2)+'\n').encode())
            instructions='''服务器累计更新（2026-10-19）

1. 在服务器原目录运行 Stop.cmd，等待 CS、MC 都正常退出。
2. 本补丁完整解压到一个新的目录，不要直接覆盖到服务端。
3. 双击 Update-Server.cmd，输入原 Start.cmd 所在目录。
4. 显示 Updated server 后，用原目录 Start.cmd 启动。

输入的是文件夹，例如 E:\\GoldCraft-Servers，不是 Start.cmd 文件或补丁解压目录。
新版遇到路径错误会提示重新输入，直接回车取消；此时尚未覆盖文件。

保留 server-settings.json、公网端口、RCON/管理员、其他配置、.state、存档及其他 Mod。
只将 mc_map_mining=2、mc_round_clear_blocks=1、mc_death_drop_items=0 写入现有 server.cfg。
清块/还原发生在下一回合开始。会去重并启用 GoldCraft/F6/僵尸规则及飘字插件，备份实际变更至 .goldcraft-updates。
需要服务器已安装 AMXX 1.10；本批 Pawn 使用官方 1.10.0.5487 编译，不替换 AMXX 核心及用户自定义 gamedata。
所有当前启用的 AMXX 二进制和翻译、飘字模型及启用清单随更新包提供，保留用户自己的插件列表与无关插件。
不必重新配置公网端口。原 package-manifest.json 保留原包基线，本次文件以 update-manifest.json 和安装备份记录为准。
玩家 CS 使用本次配套的完整图形安装包，Minecraft 整合包由服主提供。
client-update/mods 中是供整合包使用的同版 GoldCraft Core；服务端更新不能自动替换玩家文件。
若自行覆盖 payload/server，所有独立插件清单、翻译和 sprites 资源也须复制；保留自己的 plugins.ini 并避免重复启用。
'''
            if all(p in required_plugins for p in ('zp50_zombie_armor.amxx','zp50_zombie_respawn.amxx')):
                instructions+='\n本批额外包含独立 AMXX 护甲/复活插件：感染获得 2000 护甲和头盔；当前模式允许复活时，人类和僵尸死亡默认 3 秒后以僵尸复活并显示进度条。模式禁止复活或回合结束会取消等待。\n'
            if 'zp50_zombie_knockback.amxx' in required_plugins:
                instructions+='另含取消活僵尸爆头额外击退的 AMXX 插件，保留普通击退和爆头伤害。\n'
            if 'zp50_zombie_gore.amxx' in required_plugins:
                instructions+='僵尸受击喷血、隐藏头盔火花；每只累计1000实际扣血独立随机判定，zp_zombie_gib_chance默认50（百分比），成功后使用ReAPI生成内脏或头骨。未触发也消耗本次判定，余数保留。旧 goldcraft_zombie_* 被禁用，新 zp50_zombie_* 各启用一次。\n'
            if 'server/runtime/sprites/zp_infection.spr' in sources:
                instructions+='感染图标位于 cs/cstrike/sprites/zp_infection.spr，配套感染效果插件在换图时预缓存，自动加入游戏下载及内置HTTP清单。玩家仍需配套新版GoldCraft.dll；外部FastDL站需同步 sprites/zp_infection.spr。\n'
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
            instructions='''兼容脚本更新包（已安装 GoldCraft 的玩家；通常应使用完整 CS 图形安装包）

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
本次带入 1024 字节玩家资料扩容、能力握手、客户端退出崩溃和服务端向 Bot 广播时的崩溃修复。
本批加入TNT地图消息缓存/共享快照、感染☣图标、CS禁饱食度回血及ZP模式复活限制；这些项目仍需同场真人验收。
累计包含 TNT 追加更新、沙子/铁砧/火把支撑、托管输入与枪镜、移动误封计数、TACZ 倍率、死亡观战、回合开始清理、F6/ZP 和 Epic 技能保护。
配套 Core 含生命上限/属性、双向受伤效果、吸收生命、视觉效果、爆炸击退及试炼/袭击适配；飘字为每名攻击者独立最多五条。
当前文件已本地部署；实测 978 字节/130 字段传输和重连、25 角色广播及正常退出通过，用户反馈枪械/反胃正常。
公网安装、普通玩家首次配对、长期误封观察、TNT 性能及其余多人玩法仍待集中实测，不能以安装成功代替玩法验收。
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
            protocol=protocol,plugins=list(client.PLUGINS),requiredPlugins=required_plugins)
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
    parser.add_argument('--revision',required=True)
    parser.add_argument('--release',default='20261019')
    parser.add_argument('--server-only',action='store_true',help='Players use the full GUI CS installer and the owner-managed MC pack')
    args=parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}',args.revision):raise ValueError('Use full source commit')
    build(args.candidate,args.output,args.revision,args.release,args.server_only)
