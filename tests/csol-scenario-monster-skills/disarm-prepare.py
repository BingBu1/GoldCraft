"""Freeze one disarm TU and an exact reused complete-SDK link. Does not compile."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def row(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--groups', nargs='+', default=['flight', 'drop', 'selection', 'resistance', 'cancel', 'drop-cancel', 'mode'],
                        choices=['flight', 'drop', 'selection', 'resistance', 'cancel', 'drop-cancel', 'mode'])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    sdk, out = args.sdk.resolve(), args.output.resolve()
    assert sdk.is_relative_to(root / 'build') and out.is_relative_to(root / 'build')
    assert not out.exists(), 'never overwrite previous frozen inputs or evidence'
    plan = json.loads((sdk / 'prepared.json').read_text(encoding='utf-8-sig'))
    result = json.loads((sdk / 'result.json').read_text(encoding='utf-8-sig'))
    assert any(x['label'] == 'link' and x['exit'] == 0 for x in result['commands'])
    fixture = sdk / 'source/tests/csol-scenario-monster-skills/trap_fixture.hpp'
    assert 'configure(engine.files)' in fixture.read_text(encoding='utf-8-sig')
    for record in plan['inputs'] + plan['reused'] + plan['toolchain']:
        assert sha(record['path']) == record['sha256'], record['path']
    # System inputs were sampled before the earlier equipment compile. Verify
    # them again now; compilation is never used to discover unrecorded inputs.
    external = root / 'build/csol-miniboss-stall-20261030/skill8-review/equipment-check/plan-final.json'
    identity = json.loads(external.read_text(encoding='utf-8-sig'))
    system = identity['systemHeaders'] + identity['libraryInputs']
    for record in system:
        assert sha(record['path']) == record['sha256'], record['path']
    out.mkdir(parents=True)
    (out / 'source').mkdir()
    (out / 'logs').mkdir()
    source = out / 'source/disarm.cpp'
    source.write_bytes((Path(__file__).parent / 'disarm.cpp').read_bytes())
    assert source.stat().st_nlink == 1
    template = next((x for x in plan['jobs'] if x['name'] == 'trap'), plan['jobs'][0])
    target = out / 'disarm.obj'
    command = [str(source) if x == str(sdk / 'source' / template['source']) else
               '/Fo' + str(target) if x.startswith('/Fo') else x for x in template['command']]
    command.insert(-2, '/I' + str(fixture.parent))
    link = [x for x in plan['link'] if not x.lower().endswith('\\trap.obj')]
    link = ['/OUT:' + str(out / 'disarm.exe') if x.startswith('/OUT:') else x for x in link]
    link += [str(target)]
    inputs = [row(x) for x in link if x.lower().endswith(('.obj', '.lib')) and Path(x).is_absolute() and x != str(target)]
    assert all(Path(x['path']).exists() for x in inputs)
    tests = [[str(out / 'disarm.exe'), group] for group in args.groups]
    prepared = dict(preparedAt=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    candidate=plan['candidate'], sdk=str(sdk), sdkPlan=row(sdk / 'prepared.json'),
                    sdkResult=row(sdk / 'result.json'), candidateManifestSha256=plan['manifestSha256'],
                    prepareScript=row(__file__), runner=row(Path(__file__).parent / 'disarm-run.py'),
                    source=row(source), productionInputs=plan['inputs'], linkInputs=inputs,
                    systemInputs=system, toolchain=plan['toolchain'], environment=plan['environment'],
                    compile=command, link=link, tests=tests, maxJobs=1, compiled=False, executed=False,
                    boundary='Actual full SDK and production skills; controlled engine ABI; no DLL deployment or UI.')
    (out / 'prepared.json').write_text(json.dumps(prepared, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(output=str(out), source=prepared['source'], linkInputs=len(inputs),
                         systemInputs=len(system), compiled=False), ensure_ascii=False))


if __name__ == '__main__':
    main()
