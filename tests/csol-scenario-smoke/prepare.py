"""Prepare independent actual-source smoke-routing fixtures; never build or run.

Only --output is written. Use a fresh directory for each candidate. Generated
upstream source excerpts remain local evidence and must not enter publication.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re


def function(text, signature):
    start = text.index(signature)
    begin = text.index("{", start)
    token = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for match in token.finditer(text, begin):
        if match.group() == "{":
            depth += 1
        elif match.group() == "}":
            depth -= 1
            if not depth:
                return text[start:match.end()]
    raise ValueError(f"unterminated function: {signature}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = args.source_root.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite prior evidence: {output}")
    output.mkdir(parents=True)
    inputs = {}
    generated = []
    blobs = {}

    def read(relative):
        path = root / relative
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        previous = inputs.get(relative)
        if previous and previous["sha256"] != digest:
            raise RuntimeError(f"source changed during preparation: {relative}")
        inputs[relative] = {"path": relative, "sha256": digest, "size": len(data)}
        blobs[relative] = data
        return data.decode("utf-8-sig")

    def save(relative, content):
        path = output / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        data = content.encode("utf-8") if isinstance(content, str) else content
        path.write_bytes(data)
        generated.append({"path": relative, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)})

    def extract(relative, signature, target):
        body = function(read(relative), signature)
        save(target, "// Exact excerpt from " + relative + "\n" + body + "\n")
        return body

    header = read("native/csol/scenario_smoke.hpp")
    if 'EventPath[] = "events/csol_scenario_smoke.sc"' not in header:
        raise ValueError("candidate does not define the dedicated scenario smoke event")
    client = read("native/csol/client/scenario_smoke.cpp")
    registration = function(client, "void InitializeScenarioSmoke()")
    if "engine.pfnHookEvent(smoke::EventPath, CreateSmoke)" not in registration:
        raise ValueError("candidate client does not register the dedicated smoke event")

    weapon_source = read("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp")
    weapons = function(weapon_source, "void WeaponsPrecache()")
    names = re.findall(r'UTIL_PrecacheOtherWeapon\("([^"\n]+)"\)', weapons)
    if not names or len(names) != len(set(names)):
        raise ValueError("unexpected stock weapon registration list")
    classes, definitions, dispatch, registrations = [], [], [], []
    for name in names:
        relative = "external/ReGameDLL_CS/regamedll/dlls/wpn_shared/wpn_" + name.removeprefix("weapon_") + ".cpp"
        source = read(relative)
        match = re.search(r"LINK_ENTITY_TO_CLASS\(" + re.escape(name) + r",\s*(\w+)", source)
        if not match:
            raise ValueError(f"missing entity binding: {relative}")
        cls = match.group(1)
        body = function(source, "void " + cls + "::Precache()")
        fields = sorted(set(re.findall(r"\b(m_\w+)\b", body)))
        classes.append("struct " + cls + " { " + " ".join("int " + field + "{};" for field in fields) + " void Precache(); };")
        definitions.append("// Exact excerpt from " + relative + "\n" + body)
        dispatch.append('    if (std::strcmp(name, "' + name + '") == 0) { ' + cls + ' weapon; weapon.Precache(); return; }')
        registrations.append({"entity": name, "callback": cls + "::Precache", "source": relative,
                              "events": re.findall(r'PRECACHE_EVENT\(1,\s*"([^"\n]+)"\)', body)})
    save("actual_stock_precaches.inc", "\n\n".join(definitions) + "\n")
    save("actual_stock_world_precache.inc", "// Exact excerpt from external/ReGameDLL_CS/regamedll/dlls/weapons.cpp\n" + weapons + "\n")
    save("fixture_stock_declarations.inc", "\n".join(classes) + "\n")
    save("fixture_stock_dispatch.inc", "\n".join(dispatch) + '\n    throw std::runtime_error("unknown stock weapon");\n')
    indices = sorted(set(re.findall(r"\bg_sModelIndex\w+", weapons)))
    save("fixture_stock_globals.inc", "\n".join("int " + name + "{};" for name in indices) + "\n")
    extract("external/ReHLDS/rehlds/engine/pr_cmds.cpp", "unsigned short EXT_FUNC EV_Precache_internal(", "actual_engine_event.inc")

    session = read("native/csol/server/scenario_session.cpp")
    begin = session.index("    constexpr auto smokeEvent = csol::smoke::EventPath;")
    end = session.index("    if (!m_models.Precache(", begin)
    save("actual_smoke_precache.inc", "// Exact contiguous excerpt from native/csol/server/scenario_session.cpp\n" + session[begin:end])
    activate = function(session, "bool CCSOLScenarioSession::Activate()")
    branch = function(activate, "if (m_scope == CSOLScenarioResourceScope::Combat)")
    save("actual_smoke_activate.inc", "// Exact branch from native/csol/server/scenario_session.cpp\n" + branch + "\n")
    save("evidence/session-activate.txt", activate + "\n")

    server_main = read("external/ReHLDS/rehlds/engine/sv_main.cpp")
    loading = function(server_main, "int SV_SpawnServer(")
    activation = function(server_main, "void EXT_FUNC SV_ActivateServer_internal(")
    if "g_psv.state = ss_loading;" not in loading:
        raise ValueError("ReHLDS spawn no longer enters loading state")
    if activation.index("gEntityInterface.pfnServerActivate(") >= activation.index("g_psv.state = ss_active;"):
        raise ValueError("game DLL activation no longer precedes active state")
    save("evidence/engine-spawn.txt", loading + "\n")
    save("evidence/engine-activate.txt", activation + "\n")
    grenade = read("external/ReGameDLL_CS/regamedll/dlls/ggrenade.cpp")
    save("evidence/native-detonate.txt", function(grenade, "void CGrenade::__API_HOOK(SG_Detonate)()") + "\n")
    save("evidence/native-puff.txt", function(grenade, "void CGrenade::SG_Smoke()") + "\n")
    save("registration-source-order.json", json.dumps(registrations, indent=2) + "\n")

    freeze = [
        "native/csol/client/scenario_smoke.cpp", "native/csol/client/scenario_smoke.hpp",
        "native/csol/client/runtime.hpp", "native/csol/scenario_smoke.cpp",
        "native/csol/scenario_smoke.hpp", "native/csol/events/csol_scenario_smoke.sc",
    ]
    for relative in freeze:
        read(relative)
        save("source/" + relative, blobs[relative])
    # The test package is taken from this script's folder, independently of the
    # candidate source root, so previously frozen candidates can also be tested.
    test_inputs = []
    for name in ("client_routing.cpp", "event_order.cpp", "prepare.py", "README.md"):
        path = Path(__file__).resolve().parent / name
        data = path.read_bytes()
        test_inputs.append({"path": "tests/csol-scenario-smoke/" + name,
                            "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)})
        save(name, data)

    for relative, record in inputs.items():
        if hashlib.sha256((root / relative).read_bytes()).hexdigest() != record["sha256"]:
            raise RuntimeError(f"source changed during preparation: {relative}")
    receipt = {
        "prepared": True, "compiled": False, "executedTests": False, "deployed": False,
        "sourceRoot": str(root), "stockWeaponCallbacks": len(names),
        "inputs": list(inputs.values()), "testInputs": test_inputs, "generated": generated,
        "boundaries": [
            "Actual precache/activation branch bodies; full Session and ServerActivate are not executed.",
            "Modes 0/1 model shared stock precache; no AMXX/ZP plugin runs in this fixture.",
            "Client routing uses actual smoke TU and an explicit native callback observer, not stock graphics.",
            "Matching MetaHook SDK headers must be supplied and dependency hashes recorded at compilation.",
            "No compilation, rendering, runtime deployment, HTTP download or live gameplay validation.",
        ],
    }
    save("prepared.json", json.dumps(receipt, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({key: receipt[key] for key in ("prepared", "compiled", "executedTests", "deployed", "stockWeaponCallbacks")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
