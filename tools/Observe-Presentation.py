"""Passively measure delivered host presentation frames in the current scene, without changing inputs."""
import argparse
import importlib.util
import json
import time
from pathlib import Path

spec=importlib.util.spec_from_file_location('presentation_renderer',Path(__file__).with_name('Exercise-Renderer.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label',default='observe');parser.add_argument('--seconds',type=float,default=5)
    args=parser.parse_args();logs=r.ROOT/'sandbox/cs-client-b/logs';samples=[];start=time.monotonic()
    while time.monotonic()-start<args.seconds:
        samples.append({'time':time.monotonic(),'native':r.read_json(logs/'goldcraft-client-status.json'),
                        'minecraft':r.read_json(logs/'minecraft-client-status.json')})
        time.sleep(.05)
    first,last=samples[0],samples[-1];elapsed=last['time']-first['time']
    rates={}
    for source,keys in (('native',('hudFrames','hudDraws','particleFrames','entityFrames','entityPresentedFrames','entityTextureUploads','entityTextureBytes')),('minecraft',('hudFrames','particleFrames','skippedStandaloneFrames'))):
        rates[source]={key:(last[source].get(key,0)-first[source].get(key,0))/elapsed for key in keys}
    rates['entityExport']={key:(last['minecraft'].get('entityExport',{}).get(key,0)-first['minecraft'].get('entityExport',{}).get(key,0))/elapsed
                           for key in ('frames','meshBytes','textureUploads','textureBytes','sendFailures')}
    stable=all(r.same_view(first['native'],s['native']) for s in samples)
    report={'scope':__doc__,'label':args.label,'stableView':stable,'rates':rates,'samples':samples,
            'performance':r.read_json(logs/'performance-MinecraftClient.json')}
    target=r.ROOT/'analysis/goldcraft-tests'/f'presentation-{args.label}-{int(time.time())}.json'
    target.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'report':str(target),'stableView':stable,'rates':rates},indent=2))

if __name__=='__main__':main()
