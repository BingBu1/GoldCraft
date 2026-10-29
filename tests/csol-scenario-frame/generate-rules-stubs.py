"""Generate fail-fast link stubs from the pinned SDK declarations, not guessed ABI."""
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[2]
header = root/'external/ReGameDLL_CS/regamedll/dlls/gamerules.h'
text = header.read_text(encoding='utf-8')
out = ['// SPDX-License-Identifier: GPL-3.0-or-later',
    '// Test-only constructors and unrelated vtable targets. Exact SDK object',
    '// layouts and inline accessors remain real; full GameRules lifecycle is not run.',
    '#include "precompiled.h"', '#include <cstdlib>', '#include <iostream>',
    '#pragma clang diagnostic push', '#pragma clang diagnostic ignored "-Wunused-parameter"',
    'namespace { [[noreturn]] void UnusedRule(const char* name) { std::cerr << "UNEXPECTED RULES STUB: " << name << "\\n"; std::abort(); } }',
    'CGameRules::CGameRules() { m_bFreezePeriod = FALSE; m_bBombDropped = FALSE; m_bGameOver = false; m_GameDesc = nullptr; }',
    'CGameRules::~CGameRules() {}',
    'CVoiceGameMgr::CVoiceGameMgr() : m_msgPlayerVoiceMask(0), m_msgRequestState(0), m_pHelper(nullptr), m_nMaxPlayers(0), m_UpdateInterval(0) {}',
    'CVoiceGameMgr::~CVoiceGameMgr() {}',
    '// Exact SDK static storage referenced by the two unrelated inline account-rule virtual methods.',
    'RewardAccount CHalfLifeMultiplay::m_rgRewardAccountRules[RR_END]{};',
    'CHalfLifeMultiplay::CHalfLifeMultiplay() { m_bFreezePeriod = FALSE; m_bGameStarted = true; m_bRoundTerminating = false; }']
count = 0
for cls, end in [('CGameRules', '#define GAMERULES_API_INTERFACE_VERSION'), ('CHalfLifeMultiplay', 'typedef struct mapcycle_item_s')]:
    section = text.split('class '+cls+(':' if cls=='CHalfLifeMultiplay' else '\n'),1)[1].split(end,1)[0]
    for line in section.splitlines():
        s=line.split('//',1)[0].strip()
        if not re.match(r'^(virtual|VFUNC) ',s) or '{' in s or '= 0' in s or '~' in s:continue
        m=re.fullmatch(r'(?:virtual|VFUNC)\s+(.+?)([A-Za-z_][A-Za-z_0-9]*)\((.*)\);',s)
        assert m,s
        result,name,args=m.groups()
        out.append(f'{result}{cls}::{name}({args}) {{ UnusedRule("{cls}::{name}"); }}')
        count+=1
out.append('#pragma clang diagnostic pop')
(Path(__file__).parent/'rules_stubs.cpp').write_text('\n'.join(out)+'\n',encoding='utf-8')
print('generated',count,'unreachable vtable stubs from exact SDK declarations')
