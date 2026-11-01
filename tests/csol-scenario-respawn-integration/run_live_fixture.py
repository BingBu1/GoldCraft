"""Run the reviewed temporary fixture only in the idle local dedicated server.

Consumes final build/audit identities. Keeps new immutable resource files,
restores DLLs and temporary Pawn registration in finally, stops only its PID.
No graphical client is launched or controlled.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import socket
import subprocess
import time
import uuid

# Frozen copies must set GOLDCRAFT_TEST_ROOT to the intended isolated workspace.
ROOT = Path(os.environ.get('GOLDCRAFT_TEST_ROOT', Path(__file__).resolve().parents[2]))
SERVER = ROOT / 'sandbox/cs-server/Half-Life'

def read(path): return json.loads(Path(path).read_text('utf-8-sig'))
def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def identity(path):
    path = Path(path).resolve()
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path),
            'links': path.stat().st_nlink}

def verify(record):
    path = Path(record['path'])
    assert path.is_file() and path.stat().st_size == record['bytes'] and sha(path) == record['sha256'], str(path)

def safe_target(path):
    path = Path(path)
    assert path.is_absolute() and path.resolve().is_relative_to(SERVER.resolve())
    assert not any(p.is_symlink() or p.is_junction() for p in (path, *path.parents))
    return path

def atomic(source, target):
    target = safe_target(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + '.' + uuid.uuid4().hex + '.tmp')
    shutil.copy2(source, temporary)
    assert temporary.stat().st_nlink == 1 and sha(temporary) == sha(source)
    os.replace(temporary, target)

def ps_json(command):
    raw = subprocess.check_output(['pwsh', '-NoProfile', '-Command', command],
        text=True, encoding='utf-8-sig').strip()
    return json.loads(raw) if raw else None

def game_processes():
    rows = ps_json("Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('hlds.exe','MetaHook.exe','hl.exe')} | Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress")
    return [] if rows is None else [rows] if isinstance(rows, dict) else rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--server-audit', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepared, out = args.prepared.resolve(), args.output.resolve()
    assert prepared.is_relative_to(ROOT / 'build') and out.is_relative_to(ROOT / 'build') and not out.exists()
    assert not any(p.is_symlink() or p.is_junction() for p in (out, *out.parents))
    plan, compilation, audit, resources = read(prepared / 'plan.json'), read(prepared / 'compile-result.json'), read(args.server_audit), read(args.resources)
    assert compilation['success'] and compilation['exitCode'] == 0
    assert audit['success'] and audit['compileCount'] == 64 and audit['linkCount'] == 1 and audit['currentSourcesMatch']
    for record in plan['inputs']: verify(record)
    for row in plan['files']: verify(row['frozen'])
    frozen_runner = next(row['frozen'] for row in plan['files'] if row['relative'].endswith('/run_live_fixture.py'))
    assert Path(__file__).resolve() == Path(frozen_runner['path']).resolve()
    verify(plan['reapi']['runtime'])
    verify(plan['reapi']['sdk'])
    verify(plan['difficultyResource'])
    assert not Path(plan['difficultyResource']['path']).with_name('ZBS_Difficulty_zs_deadend.cso').exists()
    assert plan['reapi']['runtimeVersion'] == '5.29.0.359-dev'
    assert plan['reapi']['runtime']['sha256'] == plan['reapi']['sdk']['sha256'] == '2387f504b62d1c629a7f7b091e7b0888c8a679ea831a79fb257ce097c1af2215'
    verify(compilation['artifact'])
    assert Path(compilation['artifact']['path']) == Path(plan['futureCompile']['output'])
    products = {Path(row['path']).name.lower(): row for row in audit['products']}
    assert set(products) == {'mp.dll', 'mp.pdb'}
    for row in products.values(): verify(row)
    initial_processes = game_processes()
    assert not any(p.get('ExecutablePath') and Path(p['ExecutablePath']).resolve().is_relative_to(SERVER.resolve()) for p in initial_processes), 'Local sandbox server already in use'
    # A live original-installation client is only enumerated, never stopped.
    cluster = read(ROOT / 'sandbox/cluster.json')
    port = int(cluster['csPort'])
    assert port == 27045
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(('127.0.0.1', port))
    out.mkdir()
    (out / 'backups').mkdir()
    result = {'error': None, 'stages': [], 'commands': [], 'changed': [], 'restored': [],
        'newResourcesRetained': [], 'existingResourcesRetained': [], 'processesBefore': initial_processes,
        'serverAudit': identity(args.server_audit), 'resourcePlan': identity(args.resources),
        'preparedPlan': identity(prepared / 'plan.json'), 'runner': identity(__file__),
        'reapi': plan['reapi'], 'difficulty': plan['difficultyResource'],
        'difficultyExpectation': plan['difficultyExpectation'], 'networkMultiplayerValidated': False,
        'graphicalClientStarted': False, 'protectedInstallationModified': False}
    old_engine = identity(SERVER / 'swds.dll')
    configs = [SERVER / 'cstrike/server.cfg', SERVER / 'cstrike/goldcraft_test.cfg']
    config_before = []
    for index, path in enumerate(configs):
        record = identity(path)
        backup = out / 'backups' / ('config-' + str(index) + '.cfg')
        shutil.copy2(path, backup)
        assert sha(backup) == record['sha256'] and backup.stat().st_nlink == 1
        config_before.append(record)
    result['configurationBefore'] = config_before
    protected_root = Path('E:/Steam/steamapps/common/Half-Life')
    protected_before = {name: sha(protected_root / name) for name in ('hw.dll', 'MetaHook.exe', 'cstrike/cl_dlls/client.dll')}
    transactions = []
    process = None
    trace_path = SERVER / 'cstrike/addons/amxmodx/logs/goldcraft_scenario_respawn_test.log'
    trace_offset = trace_path.stat().st_size if trace_path.exists() else 0
    errors_before = {p.name: p.stat().st_size for p in trace_path.parent.glob('error_*.log')}
    helper_spec = importlib.util.spec_from_file_location('goldsrc_rcon', ROOT / 'tools/GoldSrc-Command.py')
    helper = importlib.util.module_from_spec(helper_spec)
    helper_spec.loader.exec_module(helper)

    def command(text):
        assert process is not None and process.poll() is None, 'Own server no longer running'
        answer = helper.command(text)
        result['commands'].append({'command': text, 'output': answer})
        return answer

    def replace(source, target):
        target = safe_target(target)
        previous = identity(target) if target.exists() else None
        backup = None
        if previous:
            backup = out / 'backups' / (str(len(transactions)) + '-' + target.name)
            shutil.copy2(target, backup)
            assert sha(backup) == previous['sha256'] and backup.stat().st_nlink == 1
        transaction = {'target': str(target), 'before': previous, 'backup': str(backup) if backup else None,
                       'newSha256': sha(source), 'applied': False}
        transactions.append(transaction)
        atomic(source, target)
        transaction['applied'] = True
        result['changed'].append({'before': previous, 'after': identity(target)})

    try:
        for row in resources['files']:
            target = safe_target(row['target'])
            assert target == SERVER / 'cstrike' / row['relative']
            assert row['relative'].split('/')[0] in ('resource', 'sound', 'sprites')
            verify(row['source'])
            if row['before']:
                assert target.exists() and sha(target) == row['before']['sha256']
                assert not row['change'] and sha(target) == row['source']['sha256']
                result['existingResourcesRetained'].append(identity(target))
            else:
                assert row['change']
                assert not target.exists(), 'Resource changed after plan: ' + str(target)
        for name, record in products.items():
            replace(record['path'], SERVER / 'cstrike/dlls' / name)
        for row in resources['files']:
            if not row['change']: continue
            assert row['before'] is None, 'This approved plan adds immutable resources only'
            atomic(row['source']['path'], Path(row['target']))
            result['newResourcesRetained'].append(identity(row['target']))
        assert len(result['newResourcesRetained']) == sum(bool(row['change']) for row in resources['files'])
        assert len(result['existingResourcesRetained']) == sum(not row['change'] for row in resources['files'])
        plugin = SERVER / 'cstrike/addons/amxmodx/plugins/goldcraft_scenario_respawn_test.amxx'
        replace(compilation['artifact']['path'], plugin)
        registration = out / 'temporary-plugin-list.ini'
        registration.write_text('goldcraft_scenario_respawn_test.amxx debug\n', encoding='ascii')
        replace(registration, SERVER / 'cstrike/addons/amxmodx/configs/plugins-csol-respawn-test.ini')
        assert identity(SERVER / 'swds.dll') == old_engine
        env = os.environ.copy()
        for suffix, key in [('PORT', 'serverPort'), ('SESSION', 'serverSession'), ('TOKEN', 'serverToken')]:
            env['GOLDCRAFT_SERVER_' + suffix] = str(cluster[key])
        env.update(GOLDCRAFT_HTTP_ENABLED='1', GOLDCRAFT_HTTP_BIND='127.0.0.1', GOLDCRAFT_HTTP_PORT='28080',
            GOLDCRAFT_HTTP_PUBLIC_URL='http://127.0.0.1:28080/goldcraft/')
        argv = [str(SERVER / 'hlds.exe'), '-game', 'cstrike', '-insecure', '-nomaster', '-console',
            '+sv_rcon_condebug', '0', '+ip', '127.0.0.1', '-port', str(port), '-maxplayers', '32',
            '-csol_game_mode', '2', '+sv_lan', '0', '+map', 'zs_deadend', '+exec', 'goldcraft_test.cfg', '+sypb_quota', '0']
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        process = subprocess.Popen(argv, cwd=SERVER, env=env, startupinfo=startup,
            creationflags=subprocess.CREATE_NEW_CONSOLE)
        result['ownPid'] = process.pid
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            assert process.poll() is None, 'Server exited during startup'
            try:
                status = command('status')
                if 'zs_deadend' in status: break
            except (TimeoutError, OSError): pass
            time.sleep(.5)
        else: raise RuntimeError('Dedicated server did not start zs_deadend')
        actual = ps_json(f"Get-CimInstance Win32_Process -Filter 'ProcessId = {process.pid}' | Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress")
        assert actual and Path(actual['ExecutablePath']).resolve() == (SERVER / 'hlds.exe').resolve()
        result['ownProcess'] = actual
        modules = ps_json(f"(Get-Process -Id {process.pid}).Modules | Where-Object {{$_.ModuleName -in @('mp.dll','swds.dll','reapi_amxx.dll')}} | Select-Object ModuleName,FileName | ConvertTo-Json -Compress")
        result['loadedModules'] = modules
        assert len(modules) == 3
        for module in modules:
            path = Path(module['FileName']).resolve()
            if module['ModuleName'].lower() == 'reapi_amxx.dll':
                assert path == Path(plan['reapi']['runtime']['path']).resolve()
                assert sha(path) == plan['reapi']['runtime']['sha256']
                continue
            expected = SERVER / ('cstrike/dlls/mp.dll' if module['ModuleName'].lower() == 'mp.dll' else 'swds.dll')
            assert path == expected.resolve()
            assert sha(path) == (products['mp.dll']['sha256'] if module['ModuleName'].lower() == 'mp.dll' else old_engine['sha256'])
        for query in ('meta list', 'amxx version', 'amxx plugins', 'sypb_quota', 'csol_game_mode', 'csol_scenario_difficulty', 'mp_forcerespawn'):
            answer = command(query)
            if query == 'meta list': assert plan['reapi']['runtimeVersion'] in answer
            if query == 'amxx version': assert '1.10.0.5487' in answer
            if query == 'amxx plugins':
                rows = [line for line in answer.splitlines() if 'CSOL automatic' in line]
                assert rows and any(re.search(r'\b(?:running|debug)\s*$', line.lower()) for line in rows)
                assert 'bad load' not in answer.lower()
        result['onlineModeChecks'] = []
        for sample in range(2):
            values = {}
            for name, expected in (('csol_game_mode', 2), ('sypb_quota', 0), ('sypb_auto_players', -1), ('sypb_quota_save', -1)):
                answer = command(name)
                match = re.search(r'"?' + name + r'"?\s+is\s+"?(-?\d+)', answer)
                assert match and int(match.group(1)) == expected, name + ': ' + answer
                values[name] = int(match.group(1))
            result['onlineModeChecks'].append(values)
            if sample == 0: time.sleep(1)
        command('csol_respawn_test 1')
        answer = command('csol_respawn_test_begin')
        assert 'refused:' not in answer.lower()
        started, deadline, last_progress, seen_stage = time.monotonic(), time.monotonic() + 720, 0.0, None
        trace = ''
        while time.monotonic() < deadline:
            assert process.poll() is None, 'Own dedicated server exited while fixture active'
            if trace_path.exists():
                with trace_path.open('rb') as stream:
                    stream.seek(trace_offset)
                    trace = stream.read().decode('utf-8', 'replace')
            stages = re.findall(r'stage=(\d+) ', trace)
            current_stage = stages[-1] if stages else None
            if current_stage != seen_stage:
                result['stages'].append({'stage': current_stage, 'seconds': time.monotonic() - started})
                seen_stage = current_stage
                print(json.dumps({'fixtureStage': current_stage, 'elapsed': round(time.monotonic() - started, 1)}), flush=True)
            # Individual checks also say result=PASS. Only a stage-level result
            # ends the fixture; never stop at fake_client_allocated.
            if re.search(r'stage=\d+ result=(PASS|FAIL|ABORT)\b', trace): break
            for path in trace_path.parent.glob('error_*.log'):
                assert path.stat().st_size == errors_before.get(path.name, 0), 'New AMXX runtime error: ' + path.name
            if time.monotonic() - last_progress >= 15:
                print(json.dumps({'waitingStage': current_stage, 'elapsed': round(time.monotonic() - started, 1)}), flush=True)
                last_progress = time.monotonic()
            time.sleep(.5)
        (out / 'fixture.log').write_text(trace, encoding='utf-8')
        assert re.search(r'stage=\d+ result=PASS\b', trace) and not re.search(r'result=(FAIL|ABORT)\b', trace), 'Fixture failed or exceeded deadline; first evidence retained'
        command('csol_respawn_test_end')
        command('csol_respawn_test 0')
        result['finalStatus'] = command('status')
        result['finalForceRespawn'] = command('mp_forcerespawn')
        assert not re.search(r'GC_Respawn_(?:Anchor|Late)', result['finalStatus'])
        result['fixturePassed'] = True
    except BaseException as error:
        result['error'] = repr(error)
    finally:
        if process is not None and process.poll() is None:
            try: command('csol_respawn_test_end')
            except Exception as error: result['cleanupCommandError'] = type(error).__name__
            try: command('quit')
            except Exception: pass
            try:
                try: process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=10)
                    result['forcedOwnPidStop'] = True
            except Exception as error:
                result['stopError'] = repr(error)
        result['ownProcessStopped'] = process is None or process.poll() is not None
        if result['ownProcessStopped']:
            for transaction in reversed(transactions):
                if not transaction['applied']: continue
                target = safe_target(transaction['target'])
                try:
                    assert target.exists() and sha(target) == transaction['newSha256'], 'Changed target cannot be overwritten: ' + str(target)
                    if transaction['backup']:
                        atomic(transaction['backup'], target)
                        assert sha(target) == transaction['before']['sha256']
                    else:
                        target.unlink() # Only this exact newly-created fixture file.
                    result['restored'].append(str(target))
                except Exception as error:
                    result.setdefault('restoreErrors', []).append(repr(error))
        if trace_path.exists():
            with trace_path.open('rb') as stream:
                stream.seek(trace_offset)
                (out / 'fixture.log').write_bytes(stream.read())
        for path in trace_path.parent.glob('error_*.log'):
            with path.open('rb') as stream:
                stream.seek(errors_before.get(path.name, 0))
                tail = stream.read()
            if tail:
                (out / ('amxx-' + path.name)).write_bytes(tail)
                result.setdefault('newAmxxErrorLogs', []).append(path.name)
        result['engineUnchanged'] = identity(SERVER / 'swds.dll') == old_engine
        result['configurationUnchanged'] = all(sha(row['path']) == row['sha256'] for row in config_before)
        result['protectedCoreUnchanged'] = all(sha(protected_root / name) == digest for name, digest in protected_before.items())
        result['resourcesStillMatch'] = all(sha(row['path']) == row['sha256'] for row in result['newResourcesRetained'] + result['existingResourcesRetained'])
        result['restorationComplete'] = len(result['restored']) == sum(t['applied'] for t in transactions) and not result.get('restoreErrors')
        (out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: result.get(key) for key in ('error', 'fixturePassed', 'ownProcessStopped', 'restorationComplete', 'engineUnchanged', 'configurationUnchanged', 'protectedCoreUnchanged')}), flush=True)
    if result['error'] or not result['restorationComplete'] or result.get('newAmxxErrorLogs'):
        raise SystemExit(1)

if __name__ == '__main__':
    main()
