"""Execute a separately approved one-scene build plan, or only verify it.

Default is read-only validation. Compilation requires the explicit execution
flag and the reviewed plan SHA. No deployment or Git operation is provided.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('crc_build', ROOT/'tools/Build-CSOLCrcBaseline.py')
crc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crc)


def response(args):
    text = subprocess.list2cmdline(args)
    if crc.split_windows('tool.exe '+text)[1:] != args:
        raise ValueError('Response argument quoting did not round-trip')
    return text+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', required=True)
    parser.add_argument('--expected-plan-sha256', required=True)
    parser.add_argument('--execute-authorized-plan', action='store_true')
    args = parser.parse_args()
    path = Path(args.plan).resolve(strict=True)
    if hashlib.sha256(path.read_bytes()).hexdigest() != args.expected_plan_sha256:
        raise ValueError('Reviewed plan identity differs')
    plan = crc.read(path)
    out = crc.safe_output(plan['output'])
    if path != out/'plan.json' or plan.get('mode') != 'single-scene-replacement':
        raise ValueError('Wrong plan or output')
    if plan['flags'] != dict(crc=True, refresh=False):
        raise ValueError('Unexpected compile configuration')
    crc.verify(plan['inputs'])
    if crc.identity(__file__) not in plan['inputs']:
        raise ValueError('Execution script not pinned by preparation')
    compile_args, link_args = plan['compile'], plan['link']
    new_object = out/'scene.cpp.obj'
    if compile_args.count('/Fo'+str(new_object)) != 1 or link_args.count(str(new_object)) != 1:
        raise ValueError('Expected one new scene object')
    if link_args[1:3] != ['-E', 'vs_link_dll'] or '/threads:1' not in link_args:
        raise ValueError('Expected single-job manifest-aware linker wrapper')
    old_objects = [r['path'] for r in plan['originalObjects']
                   if r['path'].replace('\\','/').lower().endswith('/client/scene.cpp.obj')]
    if len(old_objects) != 1:
        raise ValueError('Expected exactly one original scene object')
    old = old_objects[0]
    if any(a.lower().endswith('.obj') and crc.key(a) == crc.key(old) for a in link_args):
        raise ValueError('Original scene object was not replaced')
    # Keep /out, /pdb and /implib visible to CMake. Only direct object arguments
    # move into the response file, in their original order; libraries stay put.
    object_args = [a for a in link_args if a.lower().endswith('.obj')]
    if len(object_args) != 109:
        raise ValueError('Unexpected direct object count')
    compile_rsp, link_rsp = out/'compile.rsp', out/'link-objects.rsp'
    compile_text, link_text = response(compile_args[1:]), response(object_args)
    actual_compile = [compile_args[0], '@'+str(compile_rsp)]
    actual_link, inserted = [], False
    for arg in link_args:
        if arg.lower().endswith('.obj'):
            if not inserted:
                actual_link.append('@'+str(link_rsp))
                inserted = True
        else:
            actual_link.append(arg)
    if not args.execute_authorized_plan:
        print(json.dumps(dict(preflightPassed=True, inputRecords=len(plan['inputs']),
                             compilerExecuted=False, linkerExecuted=False,
                             responseRoundTrips=True)))
        return
    products = [out/name for name in ('scene.cpp.obj','CSOLSupport.dll','CSOLSupport.pdb','CSOLSupport.lib')]
    if any(p.exists() for p in [*products, out/'result.json', out/'started.json',
                                compile_rsp, link_rsp, out/'temp']):
        raise ValueError('Previous execution/output exists; preserve it and prepare a fresh plan')
    env = dict(os.environ)
    env.update(crc.read(plan['environment']['path']))
    env.update(plan['environmentOverrides'])
    env.update(TMP=str(out/'temp'), TEMP=str(out/'temp'))
    crc.save(out/'started.json', dict(plan=crc.identity(path), runner=crc.identity(__file__),
                                    startedUnixNs=time.time_ns(), authorizedFlag=True))
    (out/'temp').mkdir()
    compile_rsp.write_text(compile_text, encoding='utf-8')
    link_rsp.write_text(link_text, encoding='utf-8')
    result = dict(passed=False, mode=plan['mode'], commands=[],
                  plan=crc.identity(path), currentSourceBuild=False,
                  sourceRecompiled=plan['sourceRecompiled'], reusedObjectCount=212,
                  deployed=False, gameplay=False,
                  responses=[crc.identity(compile_rsp),crc.identity(link_rsp)])

    def flush():
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

    def invoke(name, argv, after_success):
        started = time.monotonic()
        try:
            done = subprocess.run(argv,cwd=plan['cwd'],env=env,capture_output=True,
                                  text=True,encoding='utf-8',errors='replace',timeout=300,
                                  creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired as error:
            def decode(value):
                return value.decode('utf-8','replace') if isinstance(value,bytes) else value or ''
            (out/(name+'.log')).write_text(decode(error.stdout)+decode(error.stderr),encoding='utf-8')
            result['commands'].append(dict(name=name,argv=argv,timedOut=True,seconds=time.monotonic()-started))
            raise
        # Anchor successful outputs before log parsing, include validation or
        # PDB inspection can fail. Failed outputs remain isolated for diagnosis.
        if done.returncode == 0:
            after_success()
        log = out/(name+'.log')
        text = done.stdout+done.stderr
        log.write_text(text,encoding='utf-8')
        result['commands'].append(dict(name=name,argv=argv,exit=done.returncode,
                                      seconds=time.monotonic()-started,log=crc.identity(log)))
        flush()
        if done.returncode:
            raise RuntimeError(name+' failed: '+str(log))
        return text

    try:
        crc.verify(plan['inputs'])
        def compiled():
            result['object'] = crc.identity(new_object)
            crc.save(out/'compiled-object.json', dict(object=result['object'],
                     plan=crc.identity(path),recordedUnixNs=time.time_ns()))
        trace = invoke('compile',actual_compile,compiled)
        included = sorted(set(re.findall(r'^\s*Note: including file:\s*(.+?)\s*$',trace,re.M)), key=crc.key)
        registered = {crc.key(row['path']):row for row in plan['sourceInputs']}
        unknown = [p for p in included if crc.key(p) not in registered]
        result['unknownIncludes'] = unknown
        if not included or unknown:
            raise RuntimeError('Actual include audit failed: '+repr(unknown))
        result['actualIncludes'] = [registered[crc.key(p)] for p in included]
        crc.verify(result['actualIncludes'])
        crc.save(out/'actual-includes.json',dict(includes=result['actualIncludes'],unknown=unknown))
        crc.verify([*plan['inputs'],result['object'],*result['responses']])
        def linked():
            result['linkedProducts'] = crc.record_link_products(out,out/'linked-products.json')
        invoke('link',actual_link,linked)
        result['verification'] = crc.inspect_relinked_variant(
            dict(name='on',output=str(out)),out/'link.log',plan,out)
        crc.verify([*plan['inputs'],result['object'],*result['linkedProducts'],*result['responses']])
        result.update(passed=True,inputsUnchanged=True,originalObjectsUnchanged=213,
                      sourceIncludeCount=len(result['actualIncludes']))
    except BaseException as error:
        result['failure'] = str(error)
        raise
    finally:
        flush()
    print(json.dumps(dict(passed=True,products=result['linkedProducts'],
                         includes=len(result['actualIncludes']),deployed=False)))


if __name__ == '__main__':
    main()
