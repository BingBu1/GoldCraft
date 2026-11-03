"""Freeze a focused actual HTTP server/store/adapter regression, without running it."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def identity(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
        sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--linker', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True,
        help='JSON object containing the x86 MSVC/Windows SDK build environment')
    parser.add_argument('--source-manifest', type=Path,
        help='Optional list of relative source paths with reviewed SHA-256 values')
    parser.add_argument('--full-suite', action='store_true',
        help='Run the existing HTTP/Store/adapter/Winsock and spray relay suite')
    parser.add_argument('--sentences', type=Path,
        help='Run only the emotion sentence-table route using the original sound/sentences.json')
    parser.add_argument('--sentences-expected', choices=('old','fixed'), default='fixed',
        help='Expected sentence route: old=404, fixed=200')
    parser.add_argument('--font', type=Path)
    parser.add_argument('--scheme', type=Path)
    args = parser.parse_args()
    if args.sentences and args.full_suite:
        parser.error('--sentences and --full-suite are mutually exclusive')
    if not args.sentences and not args.full_suite and (not args.font or not args.scheme):
        parser.error('The large-resource suite requires --font and --scheme')
    out = args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    out.mkdir(parents=True)
    inputs = []
    changed = {r['path']: r for r in read(args.source_manifest)} if args.source_manifest else {}
    files = ['tests/csol-http-large-resource/large_http.cpp',
        'tests/csol-http-large-resource/sentences_http.cpp',
        'native/engine/tests/http_download_tests.cpp','native/engine/tests/fixture/precompiled.h',
        'native/engine/http_download.h','native/engine/http_download.cpp',
        'native/engine/http_download_store.h','native/engine/http_download_store.cpp',
        'native/engine/http_download_server.h','native/engine/http_download_server.cpp',
        'native/engine/launcher_http.h','native/engine/launcher_http.cpp',
        'native/engine/precache.h','native/include/goldcraft/precache_protocol.hpp']
    for relative in files:
        source = ROOT/relative
        if relative in changed:
            assert identity(source)['sha256'] == changed[relative]['sha256']
        destination = out/'source'/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source,destination)
        inputs.append(dict(original=identity(source),frozen=identity(destination)))
    env = identity(args.environment)
    compiler, linker = args.compiler.resolve(), args.linker.resolve()
    fixture = ('native/engine/tests/http_download_tests.cpp' if args.full_suite else
        'tests/csol-http-large-resource/sentences_http.cpp' if args.sentences else
        'tests/csol-http-large-resource/large_http.cpp')
    sources = [out/'source'/fixture] + [
        out/'source/native/engine'/name for name in ('http_download.cpp','http_download_store.cpp','http_download_server.cpp','launcher_http.cpp')]
    output = out/'output'
    commands = []
    for source in sources:
        argv = [str(compiler), '/nologo','/c','/std:c++20','/EHsc','/MT','/Zi','/utf-8',
            '/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin','/clang:-ffp-contract=off',
            '--target=i686-pc-windows-msvc','/showIncludes',
            '/I'+str(out/'source/native/engine/tests/fixture'),'/I'+str(out/'source/native/engine/tests'),
            '/I'+str(out/'source/native/engine'),'/I'+str(out/'source/native/include'),
            '/Fo'+str(output/(source.stem+'.obj')),'/Fd'+str(output/'compile.pdb'),str(source)]
        commands.append(dict(name=source.stem,argv=argv,seconds=120))
    commands.append(dict(name='link',seconds=180,argv=[str(linker),'/nologo','/machine:x86','/debug',
        '/out:'+str(output/'large_http.exe'),'/pdb:'+str(output/'large_http.pdb'),'/verbose',
        *[str(output/(source.stem+'.obj')) for source in sources],'ws2_32.lib']))
    test_arguments = ([] if args.full_suite else
        [str(args.sentences.resolve()),args.sentences_expected,str(output/'run')] if args.sentences else
        [str(args.font.resolve()),str(args.scheme.resolve()),str(output/'run')])
    commands.append(dict(name='test',seconds=90,argv=[str(output/'large_http.exe'),*test_arguments]))
    plan = dict(inputs=inputs,environment=env,tools=[identity(compiler),identity(linker)],
        resources=([identity(args.sentences)] if args.sentences else [] if args.full_suite else
            [identity(args.font),identity(args.scheme)]),commands=commands,compiled=False,deployed=False,
        suite='full' if args.full_suite else 'sentences' if args.sentences else 'large',
        sentencesExpected=args.sentences_expected if args.sentences else None)
    (out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(output=str(out),inputs=len(inputs),steps=len(commands))))


if __name__=='__main__':main()
