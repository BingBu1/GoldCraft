"""Compare the native compositor with locally installed Minecraft shaders on a real GPU."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_SHA256 = {
    'post/entity_outline.json': '968a9d5dcda388a351d09aeb1872fa73b02a3411057943962f28a859742dbabd',
    'program/entity_outline.json': '6ceff6399a12249c0adb8701a3916208875e168ee561fcfe2fa0d4c863ca6c4a',
    'program/entity_outline_box_blur.json': '622b42c9411630d938d1efd8dd883613d036580dfff5e2bd877cf4849da9fa22',
    'program/blit.json': 'd1a943e9db1643e8f52af3768656a47f4e96ff5967907728b8e7780ac5e43e30',
    'program/entity_sobel.fsh': 'ca24ddd458a35458c0397d5acecf5be4794b415e67a84c6c713446a7bd62d3aa',
    'program/entity_outline_box_blur.fsh': '0ca0d091c150a5991a5c59f5c5f41ad4f1e7108fe2bd266cdc01d107ea45790d',
    'program/blit.fsh': '3bdcbb97263fbd34889e3f8f6cdb6090d08f35b82e665d827d379c6edb666f15',
    'program/sobel.vsh': 'cbb0d76e3d08f362be92d128dd341e2eab6f9026417d696e99fcdd3d107e09f9',
    'program/blur.vsh': '5ea9db3d30408c61ef4c7f8d304c31522f9cc61aa8ac741bbff659ad940a27a8',
    'program/blit.vsh': 'a9d2dda60f4b2708e9f57162ae4653963bb4fafa7b3a32718635f0e0e49fbac6',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exe', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Evidence must stay in workspace build')
    out.mkdir(parents=True, exist_ok=True)
    jar = ROOT / '.tools/neoforge-runtime/21.1.256/libraries/net/minecraft/client/1.21.1-20240808.144430/client-1.21.1-20240808.144430-extra.jar'
    reference = out / 'reference'
    reference.mkdir(exist_ok=True)
    records = {}
    names = ['post/entity_outline.json'] + ['program/' + n for n in (
        'entity_outline.json', 'entity_outline_box_blur.json', 'blit.json',
        'entity_sobel.fsh', 'entity_outline_box_blur.fsh', 'blit.fsh',
        'sobel.vsh', 'blur.vsh', 'blit.vsh')]
    with zipfile.ZipFile(jar) as z:
        for name in names:
            data = z.read('assets/minecraft/shaders/' + name)
            dst = reference / name
            dst.parent.mkdir(exist_ok=True)
            dst.write_bytes(data)
            records[name] = hashlib.sha256(data).hexdigest()
    assert records == REFERENCE_SHA256, 'Installed reference differs from pinned 1.21.1 shaders'
    graph = json.loads((reference / 'post/entity_outline.json').read_text())
    assert graph['targets'] == ['swap', 'final']
    assert [(p['name'], p['intarget'], p['outtarget']) for p in graph['passes']] == [
        ('entity_outline', 'final', 'swap'), ('entity_outline_box_blur', 'swap', 'final'),
        ('entity_outline_box_blur', 'final', 'swap'), ('blit', 'swap', 'final')]
    for name, factors in [('entity_outline', ('srcalpha', '1-srcalpha')),
                          ('entity_outline_box_blur', ('one', 'zero')), ('blit', ('srcalpha', '1-srcalpha'))]:
        blend = json.loads((reference / f'program/{name}.json').read_text())['blend']
        assert blend == dict(func='add', srcrgb=factors[0], dstrgb=factors[1])
    result = dict(jarSha256=hashlib.sha256(jar.read_bytes()).hexdigest(), resources=records,
                  boundary='Actual original fragment/vertex shaders and RGBA8 passes; synthetic silhouettes, no live game')
    if args.exe:
        run = subprocess.run([str(args.exe.resolve()), str(reference)], capture_output=True, text=True)
        (out / 'gpu.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        result['exitCode'] = run.returncode
        result['executableSha256'] = hashlib.sha256(args.exe.read_bytes()).hexdigest()
        if run.returncode:
            print(run.stdout + run.stderr)
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        assert run.returncode == 0, f'GPU test failed: {out / "gpu.log"}'
    else:
        (out / 'reference.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
