#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the actual PM_Jump test adapter without publishing upstream bodies."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve()
if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace adapter directory')
source=ROOT/'external/ReGameDLL_CS/regamedll/pm_shared/pm_shared.cpp';text=source.read_text('utf-8-sig')
marker='void EXT_FUNC __API_HOOK(PM_Jump)()\n{';start=text.index(marker);end=text.index('\n}',start)+2
body=text[start:end].replace('void EXT_FUNC __API_HOOK(PM_Jump)()','void ScenarioStaminaNativeJump()')
if body.count('CSOL_CommitActionStaminaJump')!=1 or body.count('CSOL_PrepareActionStaminaJump')!=1:
    raise ValueError('Production accepted-jump hook contract changed')
out.mkdir(parents=True);target=out/'stamina_pm_jump.inc';target.write_text(body+'\n','utf8')
(out/'receipt.json').write_text(json.dumps(dict(success=True,source=str(source),sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    adapter=str(target),adapterSha256=hashlib.sha256(target.read_bytes()).hexdigest(),
    boundary='Verbatim actual PM_Jump body with function name changed only; helper/engine boundaries supplied by SDK fixture'),indent=2)+'\n','utf8')
print('Actual PM_Jump adapter prepared')
