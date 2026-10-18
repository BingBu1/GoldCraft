"""Build the user's offline delivery ZIP from their verified original resources."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'build/floating-damage-20261018'
EXPECTED_ZIP = '38def489fb7809f66bfa197225b495990b40db10292a230ac3896eb29716c2cb'
PREFIX = 'AMXX-Floating-Damage-1.0.6/'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('upstream_zip', type=Path)
    args = parser.parse_args()
    original = args.upstream_zip.read_bytes()
    if sha(original) != EXPECTED_ZIP:
        raise ValueError('Unexpected upstream ZIP; refuse to mix another model or source version.')
    with zipfile.ZipFile(args.upstream_zip) as archive:
        model = archive.read(PREFIX + 'extra/models/x_re/floating_damage.mdl')
        language = archive.read(PREFIX + 'extra/addons/amxmodx/data/lang/floating_damage.txt')
        original_readme = archive.read(PREFIX + 'README.md')
    plugin = (OUT / 'addon_floating_damage.amxx').read_bytes()
    tests = json.loads((OUT / 'bytecode-results.json').read_text(encoding='utf-8'))
    if tests['status'] != 'passed' or tests['amxx_sha256'] != sha(plugin):
        raise ValueError('Production artifact does not match passing bytecode evidence.')
    install = '''Floating Damage 1.0.7-gc 安装说明

修复：每次伤害独立显示，不累加；每名攻击者最多同时保留5条，
第6条立即删除该攻击者最旧的一条。支持10000、100000等大数值，
最大2147483647（更大的值安全夹限）。每条约1.1秒后消失。

1. 备份原 addon_floating_damage.amxx。
2. 将本包 cstrike 内文件按原目录覆盖到服务器 cstrike。
3. 确认现有 plugins.ini 或 plugins-*.ini 只登记一次：
   addon_floating_damage.amxx
   已登记就无需再添加；未登记可参照 source/configs 中的示例。
4. 服务器必须已有 AMXX 1.9、ReAPI、ReGameDLL。
5. 正常换图加载新插件，例如 changelevel cs_assault。
   仅 sv_restart 不会重载 Pawn 插件，不需要结束 ReHLDS 进程。

保留原模型、英俄翻译、/damager 菜单、十种皮肤和爆头样式。
不需要替换 delta.lst；保留服务器已有的自定义 delta.lst。
显示的是原插件使用的 TakeDamage 参数，不改为护甲后实际血量差。
每位攻击者最多50个数字实体，未做真人满服负载验收。

已编译：AMXX 1.9.0.5303，0错误、0警告。
已验证：生产字节码25项离线回归（6022次夹具 native 调用）。
本次没有启动/重启游戏服务、没有部署；实机排版、快速连射和两种形态
显示仍需在下一次计划换图后确认。源资源属于原作者，未发现独立许可。
'''
    files = {
        '安装说明.txt': install.encode('utf-8-sig'),
        'cstrike/addons/amxmodx/plugins/addon_floating_damage.amxx': plugin,
        'cstrike/addons/amxmodx/data/lang/floating_damage.txt': language,
        'cstrike/models/x_re/floating_damage.mdl': model,
        'source/addon_floating_damage.sma': (HERE/'addon_floating_damage.sma').read_bytes(),
        'source/docs/UPSTREAM.md': original_readme,
    }
    for rel in ['include/floating_damage_events.inc', 'include/floating_damage_runtime.inc',
                'include/non_reapi_support.inc', 'configs/plugins-floating_damage.ini.example',
                'README.md', 'docs/ORIGIN.md']:
        files['source/'+rel] = (HERE/rel).read_bytes()
    manifest = {'version':'1.0.7-gc', 'upstream_zip_sha256':EXPECTED_ZIP,
                'validation':'25 production Pawn bytecode checks, fixture engine natives; no live game validation',
                'files':[{'path':name,'bytes':len(data),'sha256':sha(data)} for name,data in sorted(files.items())]}
    files['manifest.json'] = (json.dumps(manifest, ensure_ascii=False, indent=2)+'\n').encode('utf-8')
    target = ROOT/'dist/Floating-Damage-1.0.7-gc.zip'
    target.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name,data in sorted(files.items()):
            item=zipfile.ZipInfo(name, (2026,10,18,0,0,0))
            item.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(item,data)
    with zipfile.ZipFile(target) as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == set(files)
        for name,data in files.items():
            assert archive.read(name) == data, name
    result={'package':target.relative_to(ROOT).as_posix(), 'bytes':target.stat().st_size,
            'sha256':sha(target.read_bytes()), 'entries':len(files), 'verified':True,
            'plugin_sha256':sha(plugin), 'model_sha256':sha(model), 'language_sha256':sha(language)}
    (OUT/'package-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
