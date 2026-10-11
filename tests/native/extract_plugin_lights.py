"""Extract the production scene-light state and lifecycle without rewriting it.

The C++ fixture adapts only connection state, Renderer services and engine
dynamic-light allocation. Reader and light decoding remain production code.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
source=args.source.read_text(encoding='utf-8-sig')
start='std::vector<SceneLight> lamps;'
end='using Vertex=render::Vertex;'
if source.count(start)!=1 or source.count(end)!=1:
    raise ValueError('Scene-light extraction boundaries are no longer unique')
begin=source.index(start)
region=source[begin:source.index(end,begin)]
for signature in ('bool FreshLights(){','void ClearLights(){','void ReadLights(Reader& r){','void UpdateLights(){'):
    if region.count(signature)!=1:
        raise ValueError(f'Missing or ambiguous production function: {signature}')
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(region,encoding='utf-8')
print(json.dumps({'sourceSha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
                  'extractedSha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  'boundary':__doc__,'gameplayValidated':False}))
