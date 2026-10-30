"""Exact production account wrapper, original, guard stack and hook registry bodies."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def account_source(add=True):
    player = ROOT / 'external/ReGameDLL_CS/regamedll/dlls/player.cpp'
    text = player.read_text('utf-8-sig')
    start = text.index('void CBasePlayer::AddAccount(int amount, RewardType type, bool bTrackChange)')
    end = text.index('\n#endif', start)
    wrapper = text[start:end]
    start = text.index('void EXT_FUNC CBasePlayer::__API_HOOK(AddAccount)')
    end = text.index('\n#else', start)
    if add:
        original = text[start:end]
    else:
        start = text.index('void EXT_FUNC CBasePlayer::__API_HOOK(AddAccount)', end)
        end = text.index('\n#endif', start)
        original = text[start:end]
    guard = ROOT / 'native/csol/server/scenario_breakable_guard.cpp'
    body = guard.read_text('utf-8')
    body = body[body.index('thread_local CSOLBreakableScope*'):body.index('CSOLBreakableScope::Guard CSOLBreakableScope::Capture()')]
    registry = ROOT / 'external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.cpp'
    registry_body = registry.read_text('utf-8-sig')
    registry_body = registry_body[registry_body.index('AbstractHookChainRegistry::AbstractHookChainRegistry()'):]
    result = '#include "tests/csol-money/account_edges.hpp"\n' + registry_body + '\n' + body + '\n' + wrapper + '\n' + original + '\n'
    inputs = [player, guard, registry, ROOT / 'tests/csol-money/account_edges.hpp',
              ROOT / 'external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.h',
              ROOT / 'external/ReGameDLL_CS/regamedll/public/regamedll/hookchains.h',
              ROOT / 'native/csol/server/scenario_money.h', ROOT / 'native/csol/server/game_rules_gate.h',
              ROOT / 'native/csol/server/scenario_breakable_guard.h', Path(__file__).resolve()]
    return result, inputs
