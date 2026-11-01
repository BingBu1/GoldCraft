"""Execute one prepared disarm SDK suite sequentially in an assigned build window."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    args = parser.parse_args()
    out = args.prepared.resolve()
    plan = json.loads((out / 'prepared.json').read_text(encoding='utf-8-sig'))
    assert not (out / 'result.json').exists(), 'use a new frozen output for every retry'
    records = [plan['source'], plan['sdkPlan'], plan['runner']] + plan['productionInputs'] + \
              plan['linkInputs'] + plan['systemInputs'] + plan['toolchain']
    assert all(sha(x['path']) == x['sha256'] for x in records), 'input changed before build'
    environment = os.environ.copy()
    environment.update({key: value for key, value in plan['environment'].items() if isinstance(value, str)})
    result = dict(started=datetime.datetime.now(datetime.timezone.utc).isoformat(), commands=[], completed=False)

    def save():
        (out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def execute(label, command):
        (out / 'logs' / (label + '.command.json')).write_text(json.dumps(command, indent=2) + '\n', encoding='utf-8')
        log = out / 'logs' / (label + '.log')
        with log.open('w', encoding='utf-8') as stream:
            process = subprocess.Popen(command, cwd=out, env=environment, stdout=stream, stderr=subprocess.STDOUT)
            result['active'] = dict(label=label, pid=process.pid)
            save()
            code = process.wait()
        result['commands'].append(dict(label=label, exit=code, log=str(log), sha256=sha(log)))
        save()
        print(label, code, flush=True)
        return code

    try:
        if execute('compile', plan['compile']):
            return
        if execute('link', plan['link']):
            return
        for command in plan['tests']:
            execute('test-' + command[1], command)
        result['completed'] = True
        result['passed'] = all(x['exit'] == 0 for x in result['commands'])
    finally:
        result['active'] = None
        result['inputsUnchanged'] = all(sha(x['path']) == x['sha256'] for x in records)
        result['finished'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()


if __name__ == '__main__':
    main()
