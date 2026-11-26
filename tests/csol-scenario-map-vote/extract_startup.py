"""Extract unchanged Precache/Start/Reset and the real candidate support gate."""
import argparse, hashlib, json, re
from pathlib import Path
from extract_client import extract

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('adapter',type=Path);p.add_argument('gameplay_header',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    raw=a.adapter.read_bytes();text=raw.decode('utf-8-sig')
    signatures=['bool CSOL_PrecacheScenarioMapVote(', 'bool CSOL_StartScenarioMapVote(', 'void CSOL_ResetScenarioMapVote(']
    parts=[extract(text,signature) for signature in signatures]
    candidates=re.search(r'^constexpr std::array<.*> seasonOne\{\{', text, re.MULTILINE)
    if not candidates:
        raise ValueError('Missing map-vote candidate table')
    start=candidates.start()
    end=text.index('}};',start)+3;parts.insert(0,text[start:end])
    profiled='CSOL_StartScenarioMapVote(RulesProfile' in text
    args='RulesProfile::StandardVer2, DefaultSeconds' if profiled else 'DefaultSeconds'
    parts.append(f'bool Start(){{return CSOL_StartScenarioMapVote({args});}}')
    header=a.gameplay_header.read_bytes();support=extract(header.decode('utf-8-sig'),'static bool SupportsMap(')
    for name,value in [('actual_startup.inc','\n\n'.join(parts)+'\n'),('actual_support.inc',support+'\n')]:
        with (a.output/name).open('x',encoding='utf8') as f:f.write(value)
    manifest={'adapter':str(a.adapter.resolve()),'adapterSha256':hashlib.sha256(raw).hexdigest(),
              'gameplayHeader':str(a.gameplay_header.resolve()),'headerSha256':hashlib.sha256(header).hexdigest(),
              'methods':[{'signature':s,'sha256':hashlib.sha256(extract(text,s).encode()).hexdigest()} for s in signatures],
              'supportSha256':hashlib.sha256(support.encode()).hexdigest(),'profileWipIncluded':profiled}
    with (a.output/'extracted.json').open('x',encoding='utf8') as f:json.dump(manifest,f,indent=2)
if __name__=='__main__':main()
