"""Freeze the complete production cannon purchase transaction class unchanged."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
path=a.source.resolve()/'native/csol/server/weapon_service.cpp';data=path.read_bytes();text=data.decode('utf8')
chunks=[];classes=[]
for signature in ('class CannonPurchaseGuard {','class CannonPurchaseBackup {'):
    start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    assert text[end]==';';body=text[start:end+1]
    chunks.append('#line %d "%s"\n%s\n'%(text[:start].count('\n')+1,path.as_posix(),body))
    classes.append(dict(signature=signature,class_sha256=hashlib.sha256(body.encode()).hexdigest()))
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'purchase_backup.inc').write_text('\n'.join(chunks),encoding='utf8')
(a.output/'inputs.json').write_text(json.dumps(dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
    classes=classes),indent=2),encoding='utf8')
print('Extracted complete production CannonPurchaseBackup without rewriting its methods')
