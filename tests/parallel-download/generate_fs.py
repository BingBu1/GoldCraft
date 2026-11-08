"""Generate a throwing SDK adapter only in the compiler output directory."""
import re, sys
from pathlib import Path
source=Path(sys.argv[1]).read_text()
body=source.split('class IFileSystem : public IBaseInterface')[1].split('\n};')[0]
methods=[]
for line in body.splitlines():
    if 'virtual' not in line:continue
    declaration=line.strip().split('= 0;')[0].strip()
    declaration=re.sub(r'\s*=\s*0(?=\s*[,\)])','',declaration)
    methods.append(declaration+' override { throw std::runtime_error("Unexpected filesystem method"); }')
Path(sys.argv[2]).write_text('#include <stdexcept>\nclass ThrowingFS : public IFileSystem { public:\n'+'\n'.join(methods)+'\n};\n')
