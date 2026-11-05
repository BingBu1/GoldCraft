# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import ctypes,hashlib,json,sys
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(sys.argv[1]).resolve();assert OUT.is_relative_to(ROOT/'build') and not OUT.exists();OUT.mkdir(parents=True)
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def function(text,sig,start=0):
 at=text.index(sig,start);opening=text.index('{',at);depth=0
 for end in range(opening,len(text)):
  depth+=(text[end]=='{')-(text[end]=='}')
  if depth==0:return text[at:end+1]+'\n'
 raise ValueError(sig)
inputs=[];slices=[];parts=[]
studio=ROOT/'external/ReHLDS/rehlds/engine/r_studio.cpp';world=studio.with_name('world.cpp');math=studio.with_name('mathlib.cpp')
for source,signatures in [(math,['void VectorMA(','real_t _DotProduct(','qboolean VectorCompare(']),
 (studio,['void SV_InitStudioHull(','r_studiocache_t *R_CheckStudioCache(','void R_AddToStudioCache(','void SV_SetStudioHullPlane(',
 'hull_t *R_StudioHull(','int SV_HitgroupForStudioHull(','void R_InitStudioCache(','void R_FlushStudioCache(',
 'void R_StudioPlayerBlend(','hull_t *SV_HullForStudioModel(']),
 (world,['int SV_HullPointContents(','qboolean SV_RecursiveHullCheck(','void SV_SingleClipMoveToEntity('])]:
 text=source.read_text('utf-8-sig');inputs.append(identity(source))
 for sig in signatures:
  # Actual build enables REHLDS_OPT_PEDANTIC, so select its optimized body.
  at=text.index('// version with unrolled tail recursion') if sig=='qboolean SV_RecursiveHullCheck(' else 0
  body=function(text,sig,at);parts.append(body);slices.append(dict(source=str(source),signature=sig,sha256=hashlib.sha256(body.encode()).hexdigest()))
epsilon=next(line for line in world.read_text('utf-8-sig').splitlines() if line.startswith('const float DIST_EPSILON ='))
(OUT/'engine_production.inc').write_text(epsilon+'\n'+'\n'.join(parts),'utf8')
text=studio.read_text('utf-8-sig');globals=text[text.index('typedef struct r_studiocache_s'):text.index('sv_blending_interface_t svBlending')]
(OUT/'engine_globals.inc').write_text(globals,'utf8')
for name in ('engine.cpp','bridge.h'):(OUT/name).write_bytes(Path(__file__).with_name(name).read_bytes())
def split_windows(command):
 fn=ctypes.windll.shell32.CommandLineToArgvW;fn.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)];fn.restype=ctypes.POINTER(ctypes.c_wchar_p)
 count=ctypes.c_int();value=fn('tool '+command,ctypes.byref(count))
 if not value:raise ctypes.WinError()
 try:return [value[i]for i in range(1,count.value)]
 finally:
  ctypes.windll.kernel32.LocalFree.argtypes=[ctypes.c_void_p];ctypes.windll.kernel32.LocalFree(value)
meta=read(ROOT/'build/csol-oberon-walk-20261102/engine-baseline-lookup.json');flags=[]
for arg in split_windows(meta['originalCompileCommand']):
 lo=arg.lower()
 if lo.startswith(('/fo','/fd','/yu','/yc'))or lo.endswith(('.cpp','.cc')):continue
 if lo.startswith('/fp')and lo!='/fp:precise':continue
 flags.append(arg)
compiler=Path(meta['compiler']['path']);linker=compiler.with_name('lld-link.exe')
compile=[str(compiler),*flags,'/Y-','/showIncludes','/utf-8','/I'+str(ROOT/'native/engine'),'/I'+str(OUT),'/Fo'+str(OUT/'engine.obj'),str(OUT/'engine.cpp')]
link=[str(linker),'/nologo','/dll','/machine:X86','/debug','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1','/OUT:'+str(OUT/'trace.dll'),'/PDB:'+str(OUT/'trace.pdb'),str(OUT/'engine.obj')]
inputs += [identity(p)for p in [OUT/'engine.cpp',OUT/'bridge.h',OUT/'engine_globals.inc',OUT/'engine_production.inc',compiler,linker]]
(OUT/'plan.json').write_text(json.dumps(dict(commands=[compile,link],inputs=inputs,slices=slices,environment=read(meta['environment']['path']),scope='Actual ReHLDS SDK and byte-exact source bodies; real user models; bone matrices controlled; no engine process or live animation.'),indent=2))
print(OUT)
