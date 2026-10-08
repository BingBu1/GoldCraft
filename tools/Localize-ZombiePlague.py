"""Build/install Simplified Chinese ZP dictionaries without editing the supplied sources."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "external/ZombiePlague-5.0.8a/addons/amxmodx"
OUTPUT = ROOT / "dist/zombieplague/localized"
LOCALES = ("zombie_plague", "zombie_plague50")
FORMAT = re.compile(r"%(?:%|(?:[-+0 #]*\d*(?:\.\d+)?)[a-zA-Z])")
SECTION = re.compile(r"^\[([a-z]{2})\]\s*$", re.M)
LOCALIZED_SOURCES = ("zp50_buy_menus", "zp50_hud_info", "zp50_class_zombie",
                     "zp50_class_human", "zp50_items", "zp50_admin_menu", "zp50_main_menu")
KEY_SITES = {"zp50_class_zombie": 3, "zp50_class_human": 3,
             "zp50_hud_info": 2, "zp50_items": 1, "zp50_admin_menu": 1}


def safe(path):
    path = path.absolute()
    if not path.is_relative_to(ROOT) or not path.resolve().is_relative_to(ROOT):
        raise ValueError("Localization path escapes the workspace")
    for parent in (path, *path.parents):
        if parent.exists() and getattr(parent.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Refusing a reparse path: {parent}")
        if parent == ROOT:
            break
    return path


def entries(text, language, strict=True):
    result, active = {}, False
    for line in text.splitlines():
        if re.fullmatch(r"\[[a-z]{2}\]", line.strip()):
            active = line.strip() == f"[{language}]"
        elif active and "=" in line and not line.lstrip().startswith((";", "//")):
            key, value = (s.strip() for s in line.split("=", 1))
            if strict and key in result:
                raise ValueError(f"Duplicate {language} translation: {key}")
            result[key] = value
    return result


def replace_language(text, overlay):
    # Retain every other language, comment and user customization verbatim.
    matches = list(SECTION.finditer(text))
    for i, match in enumerate(matches):
        if match[1] == "cn":
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            return text[:match.start()] + overlay.rstrip() + "\n\n" + text[end:]
    return text.rstrip() + "\n\n" + overlay.rstrip() + "\n"


def parser_safe_dictionary(text, title=False):
    # AMXX 1.9 CLang uses ParseFile_INI(..., false). Its parser truncates a
    # key at the first space, and does not accept full-line comments here.
    # Keep legacy keys for other AMXX versions; our Pawn consumers prefer
    # whitespace-free aliases. Preserve all original translations byte-wise.
    sections = list(SECTION.finditer(text))
    output = []
    for index, section in enumerate(sections):
        end = sections[index + 1].start() if index + 1 < len(sections) else len(text)
        body = text[section.start():end]
        values = entries(body, section[1], strict=False)
        aliases, owners = {}, {}
        for key, value in values.items():
            if " " not in key and "\t" not in key:
                continue
            canonical = "GC_" + key.replace(" ", "_").replace("\t", "_")
            if canonical in owners and owners[canonical] != key:
                raise ValueError(f"Translation alias collision: {key} / {owners[canonical]}")
            owners[canonical] = key
            aliases[canonical] = value
        if title and section[1] == "en" and "GC_ZP_TITLE" not in values:
            aliases["GC_ZP_TITLE"] = "Zombie Plague"
        kept = []
        for line in body.splitlines():
            if line.lstrip().startswith((";", "//")):
                continue
            if "=" in line and line.split("=", 1)[0].strip() in aliases:
                continue  # Replace our prior generated aliases on updates.
            kept.append(line)
        output.append("\n".join(kept).rstrip())
        output.extend(f"{key} = {value}" for key, value in aliases.items())
        output.append("")
    return "\n".join(output) + "\n"


def dictionaries():
    result, count = {}, 0
    for name in LOCALES:
        # The supplied dictionary mixes UTF-8 with legacy bytes in unrelated
        # languages. Round-trip those bytes instead of corrupting translations.
        original = safe(ROOT / "amxx/zombie_plague/lang" / f"{name}.txt").read_text(encoding="utf-8-sig", errors="surrogateescape")
        overlay = safe(ROOT / "amxx/zombie_plague/lang" / f"{name}_cn.txt").read_text(encoding="utf-8-sig")
        english, chinese = entries(original, "en"), entries(overlay, "cn")
        missing = english.keys() - chinese.keys()
        if missing:
            raise ValueError(f"Missing Chinese keys in {name}: {sorted(missing)}")
        for key, value in english.items():
            if FORMAT.findall(value) != FORMAT.findall(chinese[key]):
                raise ValueError(f"Changed format arguments in {name}: {key}")
        # These values enter the upstream 32-byte class/description buffers.
        for key, value in chinese.items():
            if key.startswith(("ZOMBIENAME ", "ZOMBIEDESC ", "HUMANNAME ", "HUMANDESC ")) and len(value.encode()) > 31:
                raise ValueError(f"Chinese class label exceeds the actual Pawn buffer: {key}")
        merged = parser_safe_dictionary(replace_language(original, overlay), name == "zombie_plague50")
        for language in (m[1] for m in SECTION.finditer(original) if m[1] != "cn"):
            before, after = entries(original, language, strict=False), entries(merged, language, strict=False)
            if any(after.get(key) != value for key, value in before.items()):
                raise ValueError(f"Unexpected change to language {language}")
        result[name] = {"text": merged, "overlay": overlay, "keys": len(chinese)}
        count += len(chinese)
    return result, count


def replace_exact(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError(f"ZP source changed; review localization site: {old!r}")
    return text.replace(old, new)


def localized_source(name):
    source = safe(SOURCE / "scripting/zp50" / f"{name}.sma").read_text(encoding="utf-8-sig")
    if name == "zp50_buy_menus":
        source = replace_exact(source, 'register_clcmd("say /buy", "clcmd_buy")',
            'register_clcmd("say /buy", "clcmd_buy")\n\tregister_clcmd("buy", "goldcraft_native_buy")\n\tregister_clcmd("buyequip", "goldcraft_native_buy")')
        # A translated menu can exceed the old 250/300-byte buffers. AMXX's
        # actual show_menu implementation already fragments ShowMenu messages.
        source = replace_exact(source, "static menu[300], weapon_name[32]", "static menu[768], weapon_name[32], display_name[64]")
        source = replace_exact(source, "static menu[250], weapon_name[32]", "static menu[768], weapon_name[32], display_name[64]", 2)
        for group in ("primary", "secondary", "grenades"):
            line = f"ArrayGetString(g_{group}_items, index, weapon_name, charsmax(weapon_name))"
            source = replace_exact(source, line, line + "\n\t\tlocalized_weapon_name(id, weapon_name, display_name, charsmax(display_name))")
        source = replace_exact(source, "WEAPONNAMES[get_weaponid(weapon_name)]", "display_name", 3)
        source += '''
// GoldCraft localization: keep the original weapon IDs and purchase behavior.
stock localized_weapon_name(id, const classname[], name[], length)
{
    new key[96]
    formatex(key, charsmax(key), "WEAPONNAME %s", classname)
    goldcraft_translation_key(key, charsmax(key))
    if (GetLangTransKey(key) != TransKey_Bad)
        formatex(name, length, "%L", id, key)
    else
        copy(name, length, WEAPONNAMES[get_weaponid(classname)])
}

public goldcraft_native_buy(id)
{
    if (!get_pcvar_num(cvar_buy_custom_primary) && !get_pcvar_num(cvar_buy_custom_secondary)
        && !get_pcvar_num(cvar_buy_custom_grenades))
        return PLUGIN_CONTINUE;
    clcmd_buy(id)
    return PLUGIN_HANDLED;
}
'''
    elif name == "zp50_hud_info":
        source = replace_exact(source, "^nHP: %d", "^n%L: %d", 2)
        source = replace_exact(source, '"HP: %d', '"%L: %d', 2)
        source = replace_exact(source, "player_name, get_user_health(player)", 'player_name, ID_SHOWHUD, "ZOMBIE_ATTRIB1", get_user_health(player)', 2)
        source = replace_exact(source, '", get_user_health(ID_SHOWHUD)', '", ID_SHOWHUD, "ZOMBIE_ATTRIB1", get_user_health(ID_SHOWHUD)', 2)
    elif name == "zp50_main_menu":
        source = replace_exact(source, "static menu[250]", "static menu[768]")
        source = replace_exact(source, '"\\yZombie Plague %s^n^n", ZP_VERSION_STR_LONG',
                               '"\\y%L %s^n^n", id, "GC_ZP_TITLE", ZP_VERSION_STR_LONG')
        source = replace_exact(source, '"==== ^x04Zombie Plague %s^x01 ====", ZP_VERSION_STR_LONG',
                               '"==== ^x04%L %s^x01 ====", LANG_PLAYER, "GC_ZP_TITLE", ZP_VERSION_STR_LONG')
    elif name not in KEY_SITES:
        raise ValueError(f"Unexpected localization source: {name}")
    if name in KEY_SITES:
        pattern = re.compile(r'(?m)^(\s*)(formatex\(transkey, charsmax\(transkey\), "(?:ZOMBIE(?:NAME|DESC)|HUMAN(?:NAME|DESC)|ITEMNAME|MODENAME) %s", \w+\))$')
        source, count = pattern.subn(r'\1\2\n\1goldcraft_translation_key(transkey, charsmax(transkey))', source)
        if count != KEY_SITES[name]:
            raise ValueError(f"ZP translation lookup sites changed: {name}: {count}")
    if name in KEY_SITES or name == "zp50_buy_menus":
        source += '''
// Prefer AMXX 1.9 parser-safe keys; preserve legacy custom dictionary fallback.
stock goldcraft_translation_key(key[], length)
{
    new canonical[128]
    formatex(canonical, charsmax(canonical), "GC_%s", key)
    replace_all(canonical, charsmax(canonical), " ", "_")
    replace_all(canonical, charsmax(canonical), "^t", "_")
    if (GetLangTransKey(canonical) != TransKey_Bad)
        copy(key, length, canonical)
}
'''
    return source


def prepare():
    translations, count = dictionaries()
    for name, item in translations.items():
        path = safe(OUTPUT / "data/lang" / f"{name}.txt")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(item["text"], encoding="utf-8", errors="surrogateescape")
    files = {}
    for name in LOCALIZED_SOURCES:
        path = safe(OUTPUT / "scripting" / f"{name}.sma")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(localized_source(name), encoding="utf-8")
        files[name] = path
    return files, translations, count


def install():
    sources, translations, count = prepare()
    game = safe(ROOT / "sandbox/cs-server/Half-Life")
    running = subprocess.check_output(["pwsh", "-NoProfile", "-Command",
        "Get-CimInstance Win32_Process | Where-Object {$_.Name -eq 'hlds.exe'} | Select-Object -ExpandProperty ExecutablePath"], text=True)
    if str(game).lower() in running.lower():
        raise RuntimeError("Stop the recorded sandbox ReHLDS server before installing Chinese; reload it afterward")
    amxx = game / "cstrike/addons/amxmodx"
    updates = {}
    manifest = json.loads(safe(ROOT / "dist/zombieplague/manifest.json").read_text(encoding="utf-8"))
    built = {entry["name"]: entry for entry in manifest["plugins"]}
    for name in sources:
        source = safe(ROOT / built[name]["source"])
        if not source.is_relative_to(ROOT / "amxx/zombie_plague") or source.name != f"{name}.sma":
            raise ValueError("Localized source must belong to the classified Zombie Plague tree")
        plugin = safe(ROOT / "build/amxx/plugins" / f"{name}.amxx").read_bytes()
        if (built[name].get("sourceSha256") != hashlib.sha256(source.read_bytes()).hexdigest()
                or built[name]["sha256"] != hashlib.sha256(plugin).hexdigest()):
            raise ValueError(f"Recompile the current localized plugin before installing: {name}")
        updates[safe(amxx / "plugins" / f"{name}.amxx")] = plugin
    for name, item in translations.items():
        path = safe(amxx / "data/lang" / f"{name}.txt")
        original = path.read_text(encoding="utf-8-sig", errors="surrogateescape")
        updates[path] = parser_safe_dictionary(replace_language(original, item["overlay"]),
                                               name == "zombie_plague50").encode("utf-8", errors="surrogateescape")
    config = safe(amxx / "configs/amxx.cfg")
    text = config.read_text(encoding="utf-8-sig")
    for key, value in (("amx_language", '"cn"'), ("amx_client_languages", "0"), ("amx_language_display_msg", "0")):
        pattern = re.compile(r"^\s*" + re.escape(key) + r"\s+[^\r\n]*", re.M)
        text = pattern.sub(f"{key} {value}", text) if pattern.search(text) else text.rstrip() + f"\n{key} {value}\n"
    updates[config] = text.encode("utf-8")
    record = []
    for path, data in updates.items():
        previous = path.read_bytes()
        if previous != data:
            # One small original backup, retained across deployments. The
            # source dictionaries and user settings remain recoverable.
            backup = safe(ROOT / "sandbox/cs-server/localization-backup" / path.relative_to(amxx))
            if not backup.exists():
                backup.parent.mkdir(parents=True, exist_ok=True)
                backup.write_bytes(previous)
            path.write_bytes(data)
        record.append({"path": path.relative_to(ROOT).as_posix(), "sha256": hashlib.sha256(data).hexdigest()})
    report = {"language": "cn", "keys": count, "clientLanguageOverride": False, "files": record}
    safe(ROOT / "sandbox/cs-server/localization-deployment.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Installed {count} Chinese translations; applies on the next server/map startup")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true")
    args = parser.parse_args()
    if args.install:
        install()
    else:
        files, _, count = prepare()
        print(f"Verified {count} Chinese keys, placeholders, other languages and Pawn buffer lengths; prepared {len(files)} localized sources")
