"""Change a launcher's HTTP entry without changing its signed install contract."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import urllib.parse
import zipfile

spec = importlib.util.spec_from_file_location('launcher_package', Path(__file__).with_name('package.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def retarget(previous, output, server_url, version):
    previous, output = map(base.plain, (previous, output))
    if not previous.is_relative_to(base.ROOT) or not output.is_relative_to(base.ROOT) or output == base.ROOT:
        raise ValueError('Use workspace bundle and output directories')
    if output.exists():
        raise ValueError('Use a new output directory')
    uri = urllib.parse.urlsplit(server_url)
    if (uri.scheme not in ('http', 'https') or not uri.hostname or uri.username is not None
            or uri.password is not None or uri.path != '/goldcraft/' or uri.query or uri.fragment
            or any(ord(c) <= 32 or ord(c) >= 127 for c in server_url) or '\\' in server_url
            or '?' in server_url or '#' in server_url):
        raise ValueError('Expected an HTTP(S) /goldcraft/ entry without credentials')
    if uri.port is not None and not 1 <= uri.port <= 65535:
        raise ValueError('Invalid HTTP port')
    if not version or len(version) > 64 or any(ord(c) < 32 for c in version):
        raise ValueError('Invalid launcher version')

    config_path, payload = map(base.plain, (previous / 'bundle.json', previous / 'payload.zip'))
    config = json.loads(config_path.read_text('utf-8'))
    rows = config['files']
    expected = {row['path']: row for row in rows}
    if len(expected) != len(rows):
        raise ValueError('Duplicate embedded inventory')
    payload_hash = base.digest(payload)
    with zipfile.ZipFile(payload) as archive:
        if len(archive.namelist()) != len(expected) or set(archive.namelist()) != set(expected):
            raise ValueError('Embedded inventory mismatch')
        for name, row in expected.items():
            with archive.open(name) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if archive.getinfo(name).file_size != row['bytes'] or actual != row['sha256']:
                raise ValueError('Embedded payload corrupt: ' + name)

    # The key, bundle ID and all installed-file hashes remain byte-for-byte compatible.
    config.update(updateBase=server_url, version=version)
    output.mkdir(parents=True)
    os.link(payload, output / 'payload.zip')  # Immutable ZIP; never modified in place.
    if not os.path.samefile(payload, output / 'payload.zip') or base.digest(output / 'payload.zip') != payload_hash:
        raise ValueError('Immutable payload reuse failed')
    (output / 'bundle.json').write_text(json.dumps(config, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    receipt = dict(bundleId=config['bundleId'], version=version, filesVerified=len(rows),
                   previousConfigSha256=base.digest(config_path), configSha256=base.digest(output / 'bundle.json'),
                   payloadSha256=payload_hash, payloadHardlinked=True, contractUnchanged=True)
    (output / 'retarget-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf-8')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--server-url', required=True)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    print(json.dumps(retarget(args.previous, args.output, args.server_url, args.version)))
