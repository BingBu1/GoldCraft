"""Generate test-only native spans/oracle from a SHA-matched original client PE.

No game code or seed data is embedded in the repository. The generated table is
used only as an independent legacy-spread oracle inside the x86 test executable.
"""
from pathlib import Path
import argparse,hashlib,json,struct


def prepare(binary,catalog_path,output):
    binary,catalog_path,output=Path(binary),Path(catalog_path),Path(output)
    data=binary.read_bytes();catalog=json.loads(catalog_path.read_text(encoding='utf-8-sig'))
    assert hashlib.sha256(data).hexdigest()==catalog['clientSha256'],'original client identity differs'
    nt=struct.unpack_from('<I',data,0x3c)[0]
    assert data[:2]==b'MZ'and data[nt:nt+4]==b'PE\0\0'and struct.unpack_from('<H',data,nt+4)[0]==0x14c
    count=struct.unpack_from('<H',data,nt+6)[0];opt_size=struct.unpack_from('<H',data,nt+20)[0]
    image_base=struct.unpack_from('<I',data,nt+24+28)[0];sections=[]
    for i in range(count):
        virtual_size,rva,raw_size,raw=struct.unpack_from('<IIII',data,nt+24+opt_size+i*40+8)
        sections.append((rva,max(virtual_size,raw_size),raw_size,raw))
    def get(rva,size):
        section=next(s for s in sections if s[0]<=rva and rva+size<=s[0]+s[1]);delta=rva-section[0]
        copied=min(size,max(0,section[2]-delta))
        return data[section[3]+delta:section[3]+delta+copied]+bytes(size-copied)
    verified=[];spans=[]
    for row in catalog['verification']:
        rva,size=int(row['rva'],0),int(row['size'],0);blob=get(rva,size)
        assert hashlib.sha256(blob).hexdigest()==row['sha256'],row['name']
        verified.append(dict(**row,bytes=size));spans.append((rva,size,row['name']in('UTIL_SharedRandomFloat','float_to_integer'),False))
    # The matched __ftol3 implementation additionally reads these exact globals.
    # CPU level is zero in the original PE; no initialization code is executed.
    for name,rva,size,writable in [('state',int(catalog['stateRva'],0),4,True),('cpu_capability',0x13a4ac,4,True),('invalid_conversion',0xdf2f0,4,False)]:
        blob=get(rva,size);verified.append(dict(name=name,rva=hex(rva),size=hex(size),sha256=hashlib.sha256(blob).hexdigest(),rawHex=blob.hex()))
        spans.append((rva,size,False,writable))
    assert struct.unpack('<I',get(0x13a4ac,4))[0]<6
    header='inline constexpr native_random_test::Span spans[]{\n'
    header+=''.join('{%s,%d,%s,%s},\n'%(hex(a),b,str(c).lower(),str(d).lower())for a,b,c,d in spans)+'};\n'
    function=int(catalog['functions'][0]['rva'],0)
    header+=f'inline constexpr unsigned functionRva={hex(function)}, capabilityRva=0x13a4ac;\n'
    table=next(row for row in catalog['verification']if row['name']=='seed_table')
    values=struct.unpack('<256I',get(int(table['rva'],0),int(table['size'],0)))
    oracle='// Test-only original PE seed data; never compiled into CSOLSupport.\n'
    oracle+='constexpr unsigned RandomSeeds[]{'+','.join(str(n)for n in values)+'};\n'
    oracle+='''float SharedSpread(unsigned seed){
    unsigned state=RandomSeeds[seed&255];
    for(int i=0;i<3;++i){state*=69069;state+=RandomSeeds[state&255]+1;}
    return float(state&65535)/65536.f-.5f;
}
'''
    output.mkdir(parents=True,exist_ok=True)
    for name,text in [('native_spans.inc',header),('original_shared_spread.inc',oracle)]:
        with (output/name).open('x',encoding='utf8')as stream:stream.write(text)
    report=dict(binarySha256=catalog['clientSha256'],catalogSha256=hashlib.sha256(catalog_path.read_bytes()).hexdigest(),imageBase=hex(image_base),
        spans=verified,legacyOracle='Three unsigned LCG steps; original table extracted only after identity/span hash verification.',
        mapping='Explicit code/data pages at preferred image base; no loader, imports or DllMain. SSE __ftol3 levels 0/1/5.')
    with (output/'native-verification.json').open('x',encoding='utf8')as stream:json.dump(report,stream,indent=2)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True);parser.add_argument('--catalog',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    prepare(args.binary,args.catalog,args.output)
