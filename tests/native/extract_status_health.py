"""Exercise actual ReGameDLL healing methods, not a copied model of their caps."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
base = (root / 'external/ReGameDLL_CS/regamedll/dlls/cbase.cpp').read_text(encoding='utf-8-sig')
start = base.index('BOOL CBaseEntity::TakeHealth(')
end = base.index('BOOL CBaseEntity::TakeDamage(', start)
healing = base[start:end]
assert healing.count('GoldCraft_StatusMaxHealth(ENT(pev), pev->max_health)') == 2
bridge = (root / 'native/server/bridge.cpp').read_text(encoding='utf-8-sig')
start = bridge.index('float GoldCraft_StatusMaxHealth(')
end = bridge.index('\n}', start) + 2
output.write_text(bridge[start:end] + '\n' + healing, encoding='utf-8')
