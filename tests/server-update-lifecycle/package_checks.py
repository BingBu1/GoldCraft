"""Verify inherited installers and checksum closure without rebuilding issued ZIPs."""
from pathlib import Path
import argparse
import hashlib
import json
import runpy
import shutil

ROOT=Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(output):
    output=output.absolute()
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh build fixture directory')
    output.mkdir(parents=True)
    source=ROOT/'server-packages/download-recovery-20261116/GoldCraft-Server-Update-20261116-Nightfall-DownloadRecovery'
    before=digest(source/'Update-Server.ps1')
    package=output/'patch';package.mkdir()
    shutil.copy2(source/'Update-Server.ps1',package/'Update-Server.ps1')
    (package/'SHA256SUMS.txt').write_text('obsolete checksum list','utf-8')
    (package/'payload').mkdir();(package/'payload/SHA256SUMS.txt').write_text('legitimate nested payload','utf-8')
    module=runpy.run_path(str(ROOT/'tools/server-update-lifecycle/package.py'))
    module['enable'](package,refresh_sums=True)
    checks=[]
    def check(value,label):
        if not value:raise AssertionError(label)
        checks.append(label)
    check('Begin-ServerUpdate' in (package/'Update-Server.ps1').read_text('utf-8-sig'),'issued updater gains lifecycle integration')
    check(digest(package/'Lifecycle.ps1')==digest(ROOT/'tools/server-update-lifecycle/Lifecycle.ps1'),'exact shared lifecycle helper included')
    inventory={line.split('  ',1)[1]:line.split('  ',1)[0] for line in (package/'SHA256SUMS.txt').read_text().splitlines()}
    check(set(inventory)=={'Update-Server.ps1','Lifecycle.ps1','payload/SHA256SUMS.txt'} and
          all(digest(package/n)==h for n,h in inventory.items()),'old checksum list replaced by complete current members')
    old={p.relative_to(package).as_posix():digest(p) for p in package.rglob('*') if p.is_file()}
    module['enable'](package,refresh_sums=True)
    check(all(digest(package/n)==h for n,h in old.items()),'repeat integration preserves every staged byte')
    bad=output/'unsupported';bad.mkdir();(bad/'Update-Server.ps1').write_text('unknown installer')
    try:module['enable'](bad)
    except ValueError:pass
    else:raise AssertionError('unknown template accepted')
    check((bad/'Update-Server.ps1').read_text()=='unknown installer' and not (bad/'Lifecycle.ps1').exists(),'unknown installer rejected without partial conversion')
    check(digest(source/'Update-Server.ps1')==before,'issued package remains byte-identical')
    (output/'receipt.json').write_text(json.dumps(dict(success=True,checks=checks),indent=2)+'\n','utf-8')
    print(len(checks),'packaging checks passed')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args().output)
