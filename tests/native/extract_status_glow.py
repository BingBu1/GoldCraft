"""Compile the production PVS decision path, without adapting its branches."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
bridge = (root / 'native/server/bridge.cpp').read_text(encoding='utf-8-sig')
client = (root / 'external/ReGameDLL_CS/regamedll/dlls/client.cpp').read_text(encoding='utf-8-sig')
start = client.index('BOOL EXT_FUNC AddToFullPack(')
end = client.index('\tQ_memset(state, 0, sizeof(entity_state_t));', start)
prefix = client[start:end]
assert prefix.count('GoldCraft_StatusGlowVisible(ent, host)') == 1
assert prefix.index('EF_NODRAW') < prefix.index('GoldCraft_StatusGlowVisible') < prefix.index('FL_SKIPLOCALHOST')
assert prefix.index('GoldCraft_StatusGlowVisible') < prefix.index('host->v.groupinfo')
assert 'else if(message.type==Type::status_glow)HandleStatusGlow(message.payload);' in bridge
assert 'SendStatusGlow();' in bridge and 'ClearStatusGlow(true);' in bridge and 'ClearStatusGlow();' in bridge
# The omitted section only populates entity_state after all admission decisions.
# Its loop variable/argument are not otherwise used in this decision-only build.
prefix = prefix.replace('\tint i;\n', '')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(prefix + '\t(void)state;return TRUE;\n}\n', encoding='utf-8')
