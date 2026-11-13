"""Exact production account wrapper, original, guard stack and hook registry bodies."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def account_source(add=True):
    player = ROOT / 'external/ReGameDLL_CS/regamedll/dlls/player.cpp'
    text = player.read_text('utf-8-sig')
    start = text.index('void CBasePlayer::AddAccount(int amount, RewardType type, bool bTrackChange)')
    end = text.index('\n}', start) + 2
    wrapper = text[start:end]
    start = text.index('void EXT_FUNC CBasePlayer::__API_HOOK(AddAccount)')
    if not add:
        start = text.index('void EXT_FUNC CBasePlayer::__API_HOOK(AddAccount)', start + 1)
    end = text.index('\n}', start) + 2
    original = text[start:end]
    guard = ROOT / 'native/csol/server/scenario/core/scenario_breakable_guard.cpp'
    body = guard.read_text('utf-8')
    body = body[body.index('thread_local CSOLBreakableScope*'):body.index('CSOLBreakableScope::Guard CSOLBreakableScope::Capture()')]
    registry = ROOT / 'external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.cpp'
    registry_body = registry.read_text('utf-8-sig')
    registry_body = registry_body[registry_body.index('AbstractHookChainRegistry::AbstractHookChainRegistry()'):]
    result = ('#define REGAMEDLL_API\n#include "tests/csol-money/account_edges.hpp"\n'
              '#include "native/csol/scenario/custom/refund_cap.cpp"\n'
              '#include "native/csol/server/custom/damage_refund_account.cpp"\n')
    result += registry_body + '\n' + body + '\n' + wrapper + '\n' + original + '\n'
    inputs = [player, guard, registry, ROOT / 'tests/csol-money/account_edges.hpp',
              ROOT / 'external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.h',
              ROOT / 'external/ReGameDLL_CS/regamedll/public/regamedll/hookchains.h',
              ROOT / 'native/csol/server/scenario/core/scenario_money.h', ROOT / 'native/csol/server/scenario/core/game_rules_gate.h',
              ROOT / 'native/csol/server/scenario/core/scenario_breakable_guard.h',
              ROOT / 'native/csol/scenario/custom/refund_cap.hpp', ROOT / 'native/csol/scenario/custom/refund_cap.cpp',
              ROOT / 'native/csol/server/custom/damage_refund_account.h',
              ROOT / 'native/csol/server/custom/damage_refund_account.cpp', Path(__file__).resolve()]
    return result, inputs
