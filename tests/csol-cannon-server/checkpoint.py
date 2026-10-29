"""Write an exact evidence manifest after the two executable fixtures run."""
from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parents[2]
out = root / "build/csol-cannon-20261024/server-tests"
paths = list((root / "tests/csol-cannon-server").glob("*"))
paths += [root / "docs/2026-10-24_验证-cannon服务端-report.md"]
paths += list(out.glob("*.log")) + list(out.glob("*.exe"))
paths += [out / "build/compile_commands.json", out / "build/csol_cannon_server_tests.exe", out / "build/csol_cannon_pickup_tests.exe"]
for directory in ("extracted", "root-v2", "pickup-old", "pickup-fixed", "pickup-final", "purchase-backup-v4"):
    paths += list((out / directory).glob("*"))
paths += [root / "build/csol-cannon-20261024/server/mp.dll", root / "build/csol-cannon-20261024/server/mp.pdb"]
paths += list(out.glob("candidate-*.json")) + list(out.glob("server-v*-*.txt"))
case=root / "build/csol-cannon-20261024"
paths += [case/name for name in ("server-candidate-v2.json", "server-candidate-v3.json", "server-candidate-v4.json", "server-build.log", "server-build.exit",
    "deploy.py", "live-fixture.py", "live-server.py", "live-controls.py", "start-client.ps1",
    "deployment-plan.json", "deployment.json", "live-fixture-install.json", "live-controls.json",
    "pawn/csol_cannon_live_test.amxx", "pawn/csol_cannon_live_test-v1.amxx", "pawn/live-test-build.json", "pawn/live-test-build-v2.json")]
paths += [case/name for name in ("deploy-server-v4.py","update-server-v4-interface.py","update-live-fixture-draw.py","live-config.py","find-los-fixture.py","analyze-v4-live.py")]
paths += [case/name for name in ("record-model-crc-incident.py","deploy-animation.py","ordinary-cs-plan.md",
    "build-native-fixture.py","native-mode.py","live-native.py","analyze-native.py",
    "final-server-restore.json","final-error-log-identities.json","cleanup-inventory.json",
    "final-server-check.py","final-server-check.json")]
paths += [out/"deployment-plan-review.json",out/"cs-assault-breakable-bounds.json",out/"live-damage-v3-analysis.json",out/"live-v4-analysis.json",out/"los-candidates.json",out/"build/csol_cannon_purchase_backup_tests.exe"]
for directory in ("live-server", "server-v4-failed", "server-v4-runtime", "pawn-draw-runtime", "pawn-draw"):
    paths += [p for p in (case/directory).rglob('*') if p.is_file()]
for directory in ("hang-map-20261024","ordinary-cs","live-retired"):
    paths += [p for p in (case/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts]
paths += [case/'animation-runtime'/name for name in ("fatal-server-stop.json","deployment.json","deploy-animation-before-stop-guard.py")]
paths += [root/"amxx/tests/csol/csol_cannon_live_test.sma"]
for directory in ("deploy-fixture-v1", "deploy-plan-fixture-v1", "live-fixture-recovery-v1"):
    paths += list((out/directory).glob("result.json"))
    paths += list((out/directory).glob("script-under-test.py"))
paths += [root / p for p in (
    "amxx/zombie_plague/modes/zp50_gamemodes.sma",
    "amxx/zombie_plague/core/zp50_knockback.sma",
    "amxx/zombie_plague/core/zp50_zombie_damage.sma")]
records=[]
for path in sorted(set(paths)):
    if path.is_file():
        data=path.read_bytes()
        records.append(dict(path=str(path.relative_to(root)), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
results=[]
for file, count, failures in (("server-v2.log",122,0),("pickup-original-100.log",100,18),("pickup-final-100.log",100,0),("purchase-backup-v4.log",90,0)):
    text=(out/file).read_text(encoding="utf-8-sig")
    match=re.search(r"(\d+) checks; (\d+) failures",text)
    assert match and (int(match[1]),int(match[2]))==(count,failures),file
    results.append(dict(log=file,checks=count,failures=failures,negative_control=failures>0))
report=dict(scope="Extracted methods, compatible x86 builds, installer recovery and recorded single-B/SyPB real-server checks; each evidence version and negative control remains distinct",results=results,files=records)
(out/"checkpoint.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
print(f"Checkpoint: {len(records)} exact files; 122 attack + 100 pickup + 90 transaction pass; old pickup 18 expected failures")
