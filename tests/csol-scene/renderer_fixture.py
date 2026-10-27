"""Generate an abort-by-default adapter from the matching local Renderer ABI."""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
source = source[source.index('class IMetaRenderer :'):]
methods = re.findall(r'virtual\s+([^;]+?)\s*=\s*0\s*;', source)
overrides = {
    'GetCurrentSceneFBO': 'return fixture::target;',
    'GetCurrentRenderingFBO': 'return fixture::target;',
    'GetWorldMatrix': 'return fixture::world.data();',
    'GetProjectionMatrix': 'return fixture::projection.data();',
    'SetCurrentEntity': 'fixture::current = entity;',
}
lines = ['// Generated test adapter; do not publish the generated file.',
         'struct RendererFixture final : IMetaRenderer {']
for method in methods:
    name = re.search(r'(\w+)\s*\(', method).group(1)
    body = overrides.get(name, 'std::abort();')
    lines.append(f'    {method.strip()} override {{ {body} }}')
lines.append('};')
assert len(methods) > 80
Path(sys.argv[2]).write_text('\n'.join(lines) + '\n')
