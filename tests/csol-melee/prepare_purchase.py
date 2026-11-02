"""Extract unchanged inventory methods; never substitute transaction logic."""
from pathlib import Path
import hashlib
import json


def prepare(root, source):
    records = []
    generated = []
    def extract(relative, signature, end_semicolon=False):
        path = source / relative
        text = path.read_text(encoding='utf8')
        start = text.index(signature)
        at = text.index('{', start)
        end = at + 1
        depth = 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        if end_semicolon:
            assert text[end] == ';'
            end += 1
        block = text[start:end]
        records.append(dict(source=relative, signature=signature,
            sha256=hashlib.sha256(block.encode()).hexdigest()))
        return '#line %d "%s"\n%s\n' % (text[:start].count('\n')+1,
            (root/relative).as_posix(), block)
    def write(name, text):
        path = source / 'tests/csol-melee' / name
        path.write_text(text, encoding='utf8')
        generated.append(path)
    player = 'external/ReGameDLL_CS/regamedll/dlls/player.cpp'
    weapons = 'external/ReGameDLL_CS/regamedll/dlls/weapons.cpp'
    methods = [(player, signature) for signature in (
        'BOOL EXT_FUNC CBasePlayer::__API_HOOK(AddPlayerItem)',
        'BOOL EXT_FUNC CBasePlayer::__API_HOOK(RemovePlayerItem)',
        'void CBasePlayer::SelectItem(', 'BOOL CBasePlayer::SwitchWeapon(',
        'CBasePlayerItem *CBasePlayer::GetItemByName(',
        'CBasePlayerItem *CBasePlayer::GetItemById(')]
    methods += [(weapons, signature) for signature in (
        'void CBasePlayerItem::DefaultTouch(', 'int CBasePlayerItem::AddToPlayer(',
        'void CBasePlayerItem::AttachToPlayer(', 'int CBasePlayerWeapon::AddToPlayer(')]
    write('purchase_sdk_methods.inc', '\n'.join(extract(*method) for method in methods))
    write('purchase_foreach.inc', extract('external/ReGameDLL_CS/regamedll/dlls/player.h',
        'template<typename T = CBasePlayerItem, typename Functor>\n\tT *ForEachItem(const Functor &func) const'))
    write('purchase_deploy.inc', '\n'.join(extract('native/csol/server/wpn_melee.cpp', signature)
        for signature in ('void CCSOLMelee::CancelAttack()', 'BOOL CCSOLMelee::Deploy()',
            'void CCSOLMelee::Holster(', 'bool CSOL_MeleeDeploymentComplete(')))
    relative = 'native/csol/server/melee_purchase.cpp'
    text = (source/relative).read_text(encoding='utf8')
    start = text.index('namespace {')
    block = text[start:]
    records.append(dict(source=relative, signature='entire implementation after includes',
        sha256=hashlib.sha256(block.encode()).hexdigest()))
    write('purchase_actual.inc', '#line %d "%s"\n%s' % (
        text[:start].count('\n')+1, (root/relative).as_posix(), block))
    service = 'native/csol/server/weapon_service.cpp'
    write('purchase_service.inc', '\n'.join(extract(service, signature, semicolon)
        for signature, semicolon in (
            ('class WeaponPurchaseGuard {', True), ('CBasePlayer* Player(', False),
            ('const char* WeaponName(', False), ('class WeaponService final', True))))
    write('purchase-methods.json', json.dumps(records, indent=2)+'\n')
    return generated
