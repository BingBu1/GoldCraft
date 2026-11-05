"""Check the player ABI in the actual LLVM objects selected for a server link.

This reads existing bitcode only. It does not compile, link or deploy anything.
Pass complete-object-provenance.json or build-commands.json from the frozen
candidate, its exact player.h, and the independently established expected size.
The embedded debug type/header and emitted allocator calls are checked; a
source manifest alone cannot prove that a reused object used those inputs.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys
import time


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def objects_from_manifest(document):
    if isinstance(document, dict) and isinstance(document.get('units'), list):
        return [row['object'] for row in document['units']]
    if isinstance(document, dict) and isinstance(document.get('objects'), list):
        return [dict(path=row) if isinstance(row, str) else row for row in document['objects']]
    if isinstance(document, list):
        links = [row for row in document if isinstance(row, dict) and
                 row.get('kind') == 'link' and isinstance(row.get('command'), list)]
        if len(links) == 1:
            return [dict(path=arg) for arg in links[0]['command'] if arg.lower().endswith('.obj')]
    raise ValueError('Expected complete object provenance, an objects list, or one link command')


def quoted_field(record, key):
    match = re.search(r'\b' + re.escape(key) + r': "((?:\\.|[^"\\])*)"', record)
    return match[1] if match else None


def player_header(record):
    filename = quoted_field(record, 'filename')
    if not filename or filename.replace('\\', '/').rsplit('/', 1)[-1].lower() != 'player.h':
        return None
    kind = re.search(r'checksumkind: (\w+)', record)
    return dict(filename=filename, directory=quoted_field(record, 'directory'),
                kind=kind[1] if kind else None, checksum=quoted_field(record, 'checksum'), ir=record)


def allocator_calls(header, body):
    """Find the emitted GetClassPtr call through the engine allocation callback.

    The engine member offset is read from IR, not assumed. This supported x86
    lowering loads a function pointer from g_engfuncs and calls it with an edict
    pointer and an i32 allocation length. Unknown lowerings fail the check.
    """
    callbacks = {}
    for line in body:
        match = re.search(r'(%[-\w.$]+) = load ptr, ptr getelementptr\b.*?'
                          r'@"[^"\r\n]*g_engfuncs[^"\r\n]*", i32 (\d+)\)', line)
        if match:
            callbacks[match[1]] = dict(engineOffset=int(match[2]), load=line.strip())
    calls = []
    for line in body:
        for callback, load in callbacks.items():
            # Only a pointer-returning callback with (pointer, i32) arguments
            # has this allocation signature in the supported GetClassPtr body.
            pattern = (r'\b(?:call|invoke)\b[^\n]*?\bptr\s+' + re.escape(callback) +
                       r'\(ptr\s+[^,]+,\s*i32\s+(?:noundef\s+)?(-?\d+)\)')
            match = re.search(pattern, line)
            if match:
                calls.append(load | dict(bytes=int(match[1]), call=line.strip()))
    return dict(definition=header, calls=calls)


def inspect_ir(text, expected_size, header_checksums):
    nodes = {}
    definitions = []
    header = None
    body = []
    for line in text.splitlines():
        match = re.match(r'(!\d+) = (.*)', line)
        if match:
            nodes[match[1]] = match[2]
        if line.startswith('define '):
            header = line
            body = []
        elif header is not None:
            body.append(line)
            if line == '}':
                if 'GetClassPtr@VCCSPlayer@@VCBasePlayer@@@@' in header:
                    definitions.append(allocator_calls(header, body))
                header = None

    headers = []
    types = []
    failures = []
    for reference, record in nodes.items():
        if record.startswith('!DIFile('):
            parsed = player_header(record)
            if parsed:
                headers.append(parsed)
                expected = header_checksums.get(parsed['kind'])
                if not expected or parsed['checksum'] != expected:
                    failures.append(dict(kind='player-header-checksum', expected=expected, actual=parsed))
        match = re.search(r'!DICompositeType\(.*?name: "CBasePlayer".*?size: (\d+)', record)
        if not match:
            continue
        bits = int(match[1])
        fields = []
        elements = re.search(r'elements: (!\d+)', record)
        if elements:
            for child in re.findall(r'!\d+', nodes.get(elements[1], '')):
                field = nodes.get(child, '')
                if not field.startswith('!DIDerivedType(tag: DW_TAG_member,'):
                    continue
                name = quoted_field(field, 'name')
                offset = re.search(r'offset: (\d+)', field)
                if name and name.startswith('m_csol'):
                    fields.append(dict(name=name, offsetBits=int(offset[1]) if offset else 0, ir=field))
        types.append(dict(reference=reference, sizeBits=bits, sizeBytes=bits // 8, fields=fields, ir=record))
        if bits != expected_size * 8:
            failures.append(dict(kind='player-type-size', expected=expected_size, actualBits=bits, ir=record))
    if (types or definitions) and not headers:
        failures.append(dict(kind='player-definition-without-header-checksum'))
    for allocator in definitions:
        if len(allocator['calls']) != 1:
            failures.append(dict(kind='unrecognized-player-allocator', definition=allocator))
        elif allocator['calls'][0]['bytes'] != expected_size:
            failures.append(dict(kind='player-allocation-size', expected=expected_size, definition=allocator))
    return dict(types=types, playerHeaders=headers, allocators=definitions, failures=failures)


def audit(args):
    started = time.monotonic()
    manifest = identity(args.object_manifest)
    header = identity(args.player_header)
    tool = identity(args.llvm_dis)
    checker = identity(__file__)
    data = Path(header['path']).read_bytes()
    checksums = {'CSK_MD5': hashlib.md5(data).hexdigest(),
                 'CSK_SHA1': hashlib.sha1(data).hexdigest(),
                 'CSK_SHA256': header['sha256']}
    records = objects_from_manifest(json.loads(Path(manifest['path']).read_text('utf-8-sig')))
    if not records:
        raise ValueError('The link contains no object files')
    paths = [str(Path(row['path']).resolve(strict=True)).lower() for row in records]
    if len(paths) != len(set(paths)):
        raise ValueError('Duplicate object paths in the link manifest')
    rows = []
    for index, recorded in enumerate(records):
        observed = identity(recorded['path'])
        failures = []
        for field in ('bytes', 'sha256'):
            if field in recorded and recorded[field] != observed[field]:
                failures.append(dict(kind='object-identity-drift', field=field,
                                     expected=recorded[field], actual=observed[field]))
        parsed = dict(types=[], playerHeaders=[], allocators=[], failures=[])
        if not failures:
            with Path(observed['path']).open('rb') as stream:
                magic = stream.read(4)
            if magic != b'BC\xc0\xde':
                failures.append(dict(kind='unsupported-object-format', magic=magic.hex()))
            else:
                result = subprocess.run([tool['path'], observed['path'], '-o', '-'],
                                        capture_output=True, timeout=args.timeout)
                if result.returncode:
                    failures.append(dict(kind='llvm-dis-failed', exitCode=result.returncode,
                                         stderr=result.stderr.decode('utf-8', errors='replace')))
                else:
                    parsed = inspect_ir(result.stdout.decode('utf-8'), args.expected_size, checksums)
                if identity(observed['path']) != observed:
                    failures.append(dict(kind='object-changed-during-inspection'))
        rows.append(dict(object=observed, **parsed, identityFailures=failures))
        if args.verbose:
            print(f'{index + 1}/{len(records)} {Path(observed["path"]).name}: '
                  f'{len(parsed["failures"]) + len(failures)} issue(s)', flush=True)

    aggregate = []
    type_count = sum(len(row['types']) for row in rows)
    allocator_count = sum(len(row['allocators']) for row in rows)
    if not type_count:
        aggregate.append(dict(kind='no-complete-player-type'))
    if not allocator_count:
        aggregate.append(dict(kind='no-player-allocator-definition'))
    for entry in [manifest, header, tool, checker, *(row['object'] for row in rows)]:
        if identity(entry['path']) != entry:
            aggregate.append(dict(kind='input-drift', path=entry['path']))
    failed = [row for row in rows if row['failures'] or row['identityFailures']]
    return dict(schema=1, success=not failed and not aggregate,
                expectedPlayerSize=args.expected_size, expectedPlayerHeader=header,
                expectedHeaderChecksums=checksums, objectManifest=manifest, tool=tool, checker=checker,
                objectCount=len(rows), recordedTypeCount=type_count, allocatorDefinitionCount=allocator_count,
                failedObjectCount=len(failed), failedObjects=[row['object']['path'] for row in failed],
                aggregateFailures=aggregate, objects=rows, seconds=time.monotonic() - started,
                boundaries=dict(readOnly=True, compiled=False, linked=False, deployed=False,
                                liveGameplay=False,
                                noRecordedType='An object without a complete debug type is not counted as size evidence; '
                                               'any recorded player.h checksum and allocator are still checked.'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--object-manifest', type=Path, required=True)
    parser.add_argument('--player-header', type=Path, required=True)
    parser.add_argument('--expected-size', type=int, required=True)
    parser.add_argument('--llvm-dis', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=60, help='Maximum seconds for each object reader')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args()
    if args.expected_size <= 0 or args.timeout <= 0:
        parser.error('Size and timeout must be positive')
    if args.output.exists():
        parser.error('Use a new output path; existing evidence is not overwritten')
    result = audit(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(json.dumps({key: result[key] for key in
                      ('success', 'objectCount', 'recordedTypeCount', 'allocatorDefinitionCount',
                       'failedObjectCount', 'failedObjects', 'aggregateFailures', 'seconds')},
                     ensure_ascii=False, indent=2))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print('Player ABI audit could not complete: ' + str(error), file=sys.stderr)
        sys.exit(2)
