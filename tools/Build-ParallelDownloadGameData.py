"""Generate the independent downloader's exact-engine MetaHook catalog."""
import argparse, hashlib, json, struct
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent

def main():
    p=argparse.ArgumentParser();p.add_argument('--engine',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    engine=a.engine.resolve(strict=True);out=a.output.resolve()
    if not engine.is_relative_to(ROOT) or not out.is_relative_to(ROOT):raise ValueError('Workspace paths required')
    config=json.loads((ROOT/'native/parallel_download/engine_10210.json').read_text())
    data=engine.read_bytes()
    if hashlib.sha256(data).hexdigest()!=config['sha256']:raise ValueError('Unanalyzed engine identity')
    pe=struct.unpack_from('<I',data,60)[0];count=struct.unpack_from('<H',data,pe+6)[0];opt=struct.unpack_from('<H',data,pe+20)[0]
    sections=[struct.unpack_from('<IIII',data,pe+24+opt+i*40+8) for i in range(count)]
    def read(rva,size):
        for _,base,length,offset in sections:
            if base<=rva and rva+size<=base+length:return data[offset+rva-base:offset+rva-base+size]
        raise ValueError('Unbacked code')
    for check in config['verification']:
        expected=bytes.fromhex(check['bytes'])
        if read(int(check['rva'],16),len(expected))!=expected:raise ValueError('Instruction mismatch')
    for row in config['records']:
        if row['kind']!='global':continue
        f=row['payload'];site=int(f['gv_sig_va'],16)-0x10000000
        actual=read(site,int(f['gv_inst_length'],16));disp=int(f['gv_inst_disp'],16)
        if struct.unpack_from('<I',actual,disp)[0]!=int(f['gv_va'],16):raise ValueError('Global operand mismatch')
    version='parallel-download-hl-10210'
    snapshot=dict(schemaVersion=5,source=dict(snapshotSchemaVersion=8,analysisOutputContractVersion=3,gameVersion=version),binaries=dict(engine=dict(windows=dict(crc64=config['crc64']))),records=config['records'])
    payload=(json.dumps(snapshot,indent=2)+'\n').encode()
    index=dict(schemaVersion=4,versions=[dict(gameVersion=version,url=version+'.json',sha256=hashlib.sha256(payload).hexdigest(),size=len(payload),snapshotSchemaVersion=8,fileCount=len(config['records']))])
    out.mkdir(parents=True,exist_ok=True);(out/(version+'.json')).write_bytes(payload);(out/'index.json').write_text(json.dumps(index,indent=2)+'\n')
    print(json.dumps(dict(symbols=len(config['records']),verified=len(config['verification']),engineSha256=config['sha256'])))

if __name__=='__main__':main()
