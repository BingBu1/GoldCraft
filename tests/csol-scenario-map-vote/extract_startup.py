"""Extract unchanged Precache/Start/Reset and the real candidate support gate."""
import argparse, hashlib, json
from pathlib import Path
from extract_client import extract

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('adapter',type=Path);p.add_argument('gameplay_header',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    raw=a.adapter.read_bytes();text=raw.decode('utf-8-sig')
    signatures=['bool CSOL_PrecacheScenarioMapVote(', 'bool CSOL_StartScenarioMapVote(', 'void CSOL_ResetScenarioMapVote(']
    parts=[extract(text,signature) for signature in signatures]
    start=text.index('constexpr std::array<std::pair<std::uint16_t,const char*>,7> seasonOne')
    end=text.index('}};',start)+3;parts.insert(0,text[start:end])
    profiled='CSOL_StartScenarioMapVote(RulesProfile' in text
    parts.append('bool Start(){return CSOL_StartScenarioMapVote('+('RulesProfile::Scenario,' if profiled else '')+'DefaultSeconds);}')
    # Legacy deployed methods deliberately keep their own signature. Profiled
    # root WIP requires an explicit profile name resolved by its owning test.
    if profiled:raise ValueError('Use a deployed legacy adapter or provide a separately reviewed profiled fixture; no guessed profile enum.')
    header=a.gameplay_header.read_bytes();support=extract(header.decode('utf-8-sig'),'static bool SupportsMap(')
    for name,value in [('actual_startup.inc','\n\n'.join(parts)+'\n'),('actual_support.inc',support+'\n')]:
        with (a.output/name).open('x',encoding='utf8') as f:f.write(value)
    manifest={'adapter':str(a.adapter.resolve()),'adapterSha256':hashlib.sha256(raw).hexdigest(),
              'gameplayHeader':str(a.gameplay_header.resolve()),'headerSha256':hashlib.sha256(header).hexdigest(),
              'methods':[{'signature':s,'sha256':hashlib.sha256(extract(text,s).encode()).hexdigest()} for s in signatures],
              'supportSha256':hashlib.sha256(support.encode()).hexdigest(),'profileWipIncluded':False}
    with (a.output/'extracted.json').open('x',encoding='utf8') as f:json.dump(manifest,f,indent=2)
if __name__=='__main__':main()
