"""Prepare only: freeze the Pawn fixture and exact future local-server plan.

Never invokes a compiler, starts a game, uses RCON, deploys or reads secrets.
The parent owns the later serial compile/deployment/live-test window.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]

def identity(path):
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    out = parser.parse_args().output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use fresh output under workspace build')
    if any(p.is_symlink() or p.is_junction() for p in (out, *out.parents)):
        raise ValueError('Reparse output refused')
    out.mkdir(parents=True)
    names = [
        'amxx/tests/csol/goldcraft_scenario_respawn_test.sma',
        'tests/csol-scenario-respawn-integration/prepare_live_fixture.py',
        'tests/csol-scenario-respawn-integration/LIVE_RESPAWN.md',
        'tests/csol-scenario-respawn-integration/run_live_fixture.py',
    ]
    files = []
    for name in names:
        original, target = ROOT / name, out / 'source' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        assert target.stat().st_nlink == 1 and original.read_bytes() == target.read_bytes()
        files.append({'relative': name, 'original': identity(original), 'frozen': identity(target)})
    compiler = ROOT / '.tools/amxx-1.10.0.5487/addons/amxmodx/scripting/amxxpc.exe'
    reapi_runtime = ROOT / 'sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/modules/reapi_amxx.dll'
    reapi_sdk = ROOT / '.tools/reapi-5.29.0.358/addons/amxmodx/modules/reapi_amxx.dll'
    assert reapi_runtime.read_bytes() == reapi_sdk.read_bytes()
    assert identity(reapi_runtime)['sha256'] == '2387f504b62d1c629a7f7b091e7b0888c8a679ea831a79fb257ce097c1af2215'
    difficulty = ROOT / 'sandbox/cs-server/Half-Life/cstrike/maps/ZBS_Difficulty_Default.cso'
    assert identity(difficulty)['sha256'] == '418a9a78f27977ffaf5f439f5a42ae93a449de4363cfeb58ff98641fc985350b'
    assert not difficulty.with_name('ZBS_Difficulty_zs_deadend.cso').exists()
    inputs = [identity(compiler), identity(compiler.with_name('amxxpc32.dll')),
              identity(ROOT / 'tools/GoldSrc-Command.py')]
    includes = []
    for label, directory in [('amxx', compiler.parent / 'include'),
            ('reapi', ROOT / '.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include')]:
        destination = out / 'includes' / label
        destination.mkdir(parents=True)
        for original in sorted(directory.rglob('*.inc')):
            target = destination / original.relative_to(directory)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, target)
            assert target.stat().st_nlink == 1 and original.read_bytes() == target.read_bytes()
            inputs.append(identity(target))
        includes.append(destination)
    declaration_pairs = []
    for name in ('reapi.inc', 'reapi_gamedll_const.inc', 'reapi_gamedll.inc', 'reapi_engine.inc', 'cssdk_const.inc'):
        source = ROOT / 'external/ReAPI/reapi/extra/amxmodx/scripting/include' / name
        frozen = out / 'includes/reapi' / name
        assert source.read_bytes() == frozen.read_bytes(), name
        declaration_pairs.append({'readSource': identity(source), 'compileHeader': identity(frozen)})
    for name in ('amxmodx.inc', 'fakemeta.inc', 'fakemeta_const.inc', 'hamsandwich.inc', 'ham_const.inc', 'messages.inc'):
        source = ROOT / 'external/amxmodx/plugins/include' / name
        frozen = out / 'includes/amxx' / name
        assert source.read_bytes() == frozen.read_bytes(), name
        declaration_pairs.append({'readSource': identity(source), 'compileHeader': identity(frozen)})
    frozen_source = out / 'source' / names[0]
    artifact = out / 'output/goldcraft_scenario_respawn_test.amxx'
    command = [str(compiler), str(frozen_source), '-o' + str(artifact), '-d2', *('-i' + str(p) for p in includes)]
    plan = {
        'mode': 'prepared_only', 'compiled': False, 'deployed': False, 'launched': False,
        'scope': 'Local dedicated real GameDLL lifecycle with two owned fake clients; never network multiplayer or pixel acceptance.',
        'files': files, 'inputs': inputs, 'matchedDeclarations': declaration_pairs,
        'difficultyResource': identity(difficulty),
        'difficultyExpectation': {'row': 1, 'column6': 0.1, 'minimumBaseLevel': 1,
            'minimumDelaySeconds': 1.5, 'readOnly': True},
        'reapi': {'runtimeVersion': '5.29.0.359-dev', 'sdkDirectoryVersion': '5.29.0.358',
            'runtime': identity(reapi_runtime), 'sdk': identity(reapi_sdk),
            'matchingBytes': True, 'reference': 'docs/NATIVE_HEALTH_LIMITS.md; docs/ZOMBIE_TESTING.md; amxx/nade_modes/README.md'},
        'futureCompile': {'command': command, 'cwd': str(out), 'timeoutSeconds': 120,
            'output': str(artifact), 'createOutputDirectoryBeforeRun': True,
            'requirements': ['single-job window released by elite/UI full build', 'all exact input SHA unchanged',
                'compile exit 0 and no warnings/errors', 'save complete first failure log; never overwrite this preparation']},
        'futureActivation': {
            'serverRoot': str(ROOT / 'sandbox/cs-server/Half-Life'),
            'map': 'zs_deadend', 'mode': 2, 'slots': 32, 'sypbQuota': 0,
            'requiredRuntime': 'Parent rebuilt matched automatic-revival GameDLL; record actual loaded module SHA before claiming results.',
            'runner': str(out / 'source/tests/csol-scenario-respawn-integration/run_live_fixture.py'),
            'runnerSourceRoot': str(ROOT),
            'temporaryPluginList': 'cstrike/addons/amxmodx/configs/plugins-csol-respawn-test.ini',
            'temporaryPluginLine': 'goldcraft_scenario_respawn_test.amxx debug',
            'localCommands': ['amxx version', 'meta list', 'amxx plugins', 'csol_respawn_test 1',
                'csol_respawn_test_begin'],
            'preconditions': ['empty isolated dedicated server, no user or other fake client',
                'map fresh/not game-started; normal AMXX reload by map change, not sv_restart',
                'previous mp_forcerespawn and temporary plugin/config identities preserved',
                'ReAPI SDK 5.29.0.358 archive contains exact matched runtime 5.29.0.359-dev; DLL SHA and AMXX 1.10.0.5487 verified'],
        },
        'observations': [
            'Anchor normal jointeam/joinclass, true game start and natural first Spawn; wait freeze to end.',
            'Late client joins only after active round. Observe original AutoRespawnWait countdown and exactly one true native Spawn, no Killed needed.',
            'Trigger only native Killed for owned late client. Wait actual death animation/deadtime/observer/countdown and exactly one spawn.',
            'Repeat with mp_forcerespawn 0.1; the native forcing path must not beat the announced scenario deadline.',
            'Kill owned late client again. Once its countdown starts, kill last anchor; wait remaining announced timer plus 5 seconds. Both must remain dead and round terminating.',
            'Confirm no round-start time changed, no forced respawn invocation, no ownership drift; cleanup exact owned slots and original force cvar.',
        ],
        'limits': {'phaseSeconds': 180, 'suggestedExternalWatchdogSeconds': 900,
            'sampleRateHz': 1, 'logName': 'goldcraft_scenario_respawn_test.log',
            'fakeNames': ['GC_Respawn_Anchor', 'GC_Respawn_Late'],
            'manualAbortCommand': 'csol_respawn_test_end'},
        'cleanup': ['Always issue csol_respawn_test_end on external timeout/interruption while server is available.',
            'Plugin restores DAMAGE_NO only for exact owned userid+edict serial and drops those clients with rh_drop_client.',
            'Plugin restores mp_forcerespawn only if it still equals the last fixture value; unexpected external changes are logged.',
            'After evidence, disable/remove only this temporary plugin registration, preserving prior bytes; map reload unloads Pawn.',
            'Restore original saved per-map/test configuration and verify zero fixture clients/temporary protections.',
            'Keep the reviewed 26 newly added immutable resources and record hashes; restore actual local old mp/PDB, never substitute a different historical server.',
            'Do not remove user maps, production logs, original installation or shared resource files.'],
        'remaining': ['Compile not run', 'Actual local server run not performed',
            'Real authenticated late-joining client, HUD pixels and multiple network players not validated'],
    }
    path = out / 'plan.json'
    path.write_text(json.dumps(plan, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'plan': identity(path), 'sourceCount': len(files), 'registeredInputs': len(inputs),
        'matchingDeclarations': len(declaration_pairs), 'compilerInvoked': False}))

if __name__ == '__main__':
    main()
