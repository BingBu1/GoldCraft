from pathlib import Path
import os, shutil, subprocess, socket, ctypes, time
test_dir=Path(__file__).resolve().parent
root=test_dir.parents[1]
area=root/'build/scp682'
obj=area/'engine-test-obj'
obj.mkdir(parents=True,exist_ok=True)
source_cpp=(root/'external/hlsdk-amxx-sdk/dlls/animation.cpp').read_text(errors='replace')
start=source_cpp.index('int GetAnimationEvent(')
end=source_cpp.index('\n}',start)+2
(obj/'sdk_event.inc').write_text(source_cpp[start:end]+'\n',encoding='utf8')
subprocess.run(['cmake','-S',str(test_dir),'-B',str(obj),'-G','Visual Studio 16 2019','-A','Win32'],check=True)
subprocess.run(['cmake','--build',str(obj),'--config','Release'],check=True)

engine=area/'engine-run';engine.mkdir(exist_ok=True)
assert engine.resolve().is_relative_to(root/'build') and not (engine/'.foreign').exists()
source=root/'sandbox/cs-server/Half-Life'
def copy(a,b):
    b.parent.mkdir(exist_ok=True,parents=True)
    from tempfile import NamedTemporaryFile
    if b.is_file() and a.read_bytes() == b.read_bytes():
        return
    with NamedTemporaryFile(dir=b.parent, delete=False) as temp:
        candidate = Path(temp.name)
    try:
        shutil.copyfile(a,candidate)
        candidate.replace(b)
    finally:
        candidate.unlink(missing_ok=True)
for name in ('hlds.exe','swds.dll','filesystem_stdio.dll','steam_api.dll','steamclient.dll','tier0_s.dll','vstdlib_s.dll','steam_appid.txt','steam.inf','Core.dll','debug.log'):
    if name=='debug.log': continue
    f=source/name
    if f.is_file():copy(f,engine/name)
for name in ('maps/bingmap.bsp','halflife.wad','language.cfg','controller.cfg','delta.lst','decals.wad','gfx.wad','valve.rc','steam.inf','sprites/zerogxplode.spr','models/player.mdl','gfx/palette.lmp','gfx/colormap.lmp'):
    f=source/'valve'/name
    if f.is_file():copy(f,engine/'valve'/name)
game=engine/'checkmdl';game.mkdir(exist_ok=True)
copy(area/'compile/scp682.mdl',game/'models/scp682/scp682.mdl')
for sample in sorted((area/'compile/sound/scp682').glob('*.wav')):
    copy(sample,game/'sound/scp682'/sample.name)
copy(obj/'Release/checkmodel.dll',game/'dlls/checkmodel.dll')
(game/'liblist.gam').write_text('game "SCP-682 model check"\nversion "1.0"\ntype "multiplayer_only"\nsecure "0"\ngamedll "dlls/checkmodel.dll"\n',encoding='ascii')
# The test module queries the engine's evaluated bones in ServerActivate, then exits itself.
ctypes.windll.kernel32.SetErrorMode(3)
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
    sock.bind(('127.0.0.1',0));port=str(sock.getsockname()[1])
cmd=[str(engine/'hlds.exe'),'-console','-game','checkmdl','-insecure','-nomaster','-noipx','+ip','127.0.0.1','-port',port,'-condebug','+sv_lan','1','+maxplayers','2','+map','bingmap']
startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
for f in (engine/'model-test.log',game/'qconsole.log',engine/'qconsole.log'):
    if f.exists(): f.unlink()
proc=subprocess.Popen(cmd,cwd=engine,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
try: rc=proc.wait(timeout=11)
except subprocess.TimeoutExpired:
    proc.terminate();rc=proc.wait(5);print('fixture timeout',flush=True)
report=engine/'model-test.log'
s=report.read_text(errors='replace') if report.exists() else ''
print('engine exit',rc,s)
if rc!=0 or 'PASS' not in s or len([v for v in s.splitlines() if v.startswith('SEQUENCE ')])!=5:
    for f in (game/'qconsole.log',engine/'qconsole.log'):
        if f.is_file():print(f.read_text(errors='replace')[-2200:])
    raise SystemExit(1)
(area/'engine-validation.log').write_text(s,encoding='utf-8')
