"""Compare actual ReHLDS move execution and accounting, including burst protection.

Extracts the production replay block and rate-limiter methods. Engine callbacks
are observing adapters: this does not send packets or start a game server.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT/'build/move-command-rate-tests')
OUT = parser.parse_args().output.resolve()
if not OUT.is_relative_to(ROOT/'build') or any(p.is_symlink() or p.is_junction() for p in [OUT,*OUT.parents]):
    raise ValueError('Test output must be an unredirected workspace build directory')
OUT.mkdir(parents=True,exist_ok=True)
security = ROOT / 'external/ReHLDS/rehlds/rehlds/rehlds_security.cpp'
header = ROOT / 'external/ReHLDS/rehlds/rehlds/rehlds_security.h'
user = ROOT / 'external/ReHLDS/rehlds/engine/sv_user.cpp'
src = security.read_text()
decl = header.read_text()
parse = user.read_text().split('void SV_ParseMove(client_t *pSenderClient)', 1)[1]
count = parse.split('int numCmdsToIssue = numcmds;', 1)[1].split('g_MoveCommandRateLimiter.MoveCommandsIssued', 1)[0]
assert 'if (net_drop > 0 && net_drop < 24)' in count
candidate = count
count = count.replace('net_drop > 0 && net_drop < 24', 'net_drop > 0')
replay = 'if (net_drop < 24)' + parse.split('if (net_drop < 24)', 1)[1].split('#ifdef REHLDS_FIXES', 1)[0]
methods = src.split('CMoveCommandRateLimiter::CMoveCommandRateLimiter()', 1)[1].split('CStringCommandsRateLimiter::CStringCommandsRateLimiter()', 1)[0]
decl = 'class CMoveCommandRateLimiter' + decl.split('class CMoveCommandRateLimiter', 1)[1].split('extern CMoveCommandRateLimiter', 1)[0]
cvars = '\n'.join(line for line in src.splitlines() if line.startswith('cvar_t sv_rehlds_movecmdrate_'))
prelude = r'''
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <stdexcept>
#include <algorithm>
#define MAX_CLIENTS 32
#define Q_memset std::memset
#define FALSE false
#define TRUE true
struct cvar_t {const char* name;const char* text;int flags;float value;void* next;};
struct usercmd_t {};
struct client_t {bool active=true;bool connected=true;const char* name="fixture";struct {int remote_address=0;int incoming_sequence=0;} netchan;usercmd_t lastcmd;};
struct {client_t clients[MAX_CLIENTS];} g_psvs;
client_t* host_client=&g_psvs.clients[0];
double realtime=0;
unsigned drops=0,executed=0,checks=0;
void Con_DPrintf(const char*, ...){}
void SV_DropClient(client_t* c,bool,const char*){c->connected=false;++drops;}
void Cbuf_AddText(const char*){}
const char* NET_BaseAdrToString(int){return "127.0.0.1";}
const char* va(const char*,...){return "fixture";}
void SV_RunCmd(usercmd_t*,int,bool){++executed;}
void check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
'''
functions = ('int originalCount(int numcmds,int net_drop){int numCmdsToIssue=numcmds;' + count + 'return numCmdsToIssue;}\n'
             'int candidateCount(int numcmds,int net_drop){int numCmdsToIssue=numcmds;' + candidate + 'return numCmdsToIssue;}\n'
             'int actualCount(int numcmds,int net_drop,int numbackup){executed=0;int i;usercmd_t cmds[64];' + replay + 'return executed;}\n')
main = r'''
int main(){
  for(int numcmds=0;numcmds<63;++numcmds)for(int backup=0;backup+numcmds<63;++backup)for(int dropped=-1;dropped<1500;++dropped){
    check(candidateCount(numcmds,dropped)==actualCount(numcmds,dropped,backup),"candidate differs from actual SV_RunCmd count");
  }
  check(originalCount(1,1199)==1200,"old count");
  check(actualCount(1,1199,2)==1,"skipped recovery");
  realtime=0;CMoveCommandRateLimiter oldLimiter;drops=0;host_client->connected=true;
  realtime=.01;oldLimiter.MoveCommandsIssued(0,originalCount(1,1199));check(drops==1,"negative-control gap must ban");
  realtime=0;CMoveCommandRateLimiter candidateLimiter;drops=0;host_client->connected=true;
  realtime=.01;candidateLimiter.MoveCommandsIssued(0,candidateCount(1,1199));check(drops==0,"candidate gap banned");
  realtime=0;CMoveCommandRateLimiter floodLimiter;drops=0;host_client->connected=true;
  realtime=.01;for(int i=0;i<40 && host_client->connected;++i)floodLimiter.MoveCommandsIssued(0,candidateCount(30,0));check(drops==1,"genuine new-command burst escaped");
  realtime=0;CMoveCommandRateLimiter replayLimiter;drops=0;host_client->connected=true;
  realtime=.01;for(int i=0;i<50 && host_client->connected;++i)replayLimiter.MoveCommandsIssued(0,candidateCount(1,23));check(drops==1,"genuine replay workload escaped");
  realtime=0;CMoveCommandRateLimiter normalLimiter;drops=0;host_client->connected=true;
  for(int i=1;i<=3600;++i){realtime=i/60.0;normalLimiter.Frame();normalLimiter.MoveCommandsIssued(0,candidateCount(2,0));}
  check(drops==0,"normal 60 packets per second / 2 commands each banned");
  std::printf("{\"passed\":true,\"checks\":%u,\"negative_control_banned\":true,\"candidate_gap_banned\":false,\"genuine_flood_banned\":true,\"replay_flood_banned\":true,\"normal_stream_banned\":false}\n",checks);
}
'''
(OUT/'move-count-fixture.cpp').write_text(prelude + cvars + '\n' + decl + '\nCMoveCommandRateLimiter::CMoveCommandRateLimiter()' + methods + '\n' + functions + main, encoding='utf-8')
(OUT/'source-hashes.json').write_text(json.dumps({str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [security,header,user]},indent=2),encoding='utf-8')
print('Generated fixture from actual SV_ParseMove replay block and CMoveCommandRateLimiter methods; production source unchanged.')
def ps(value):return "'"+str(value).replace("'","''")+"'"
script=OUT/'run.ps1'
script.write_text('\n'.join([
    "$ErrorActionPreference='Stop'",
    '. '+ps(ROOT/'tools/SandboxPaths.ps1'),
    '. '+ps(ROOT/'tools/ClangToolchain.ps1'),
    '$moveCompiler=Initialize-GoldCraftCompiler',
    "& (Join-Path $moveCompiler.Root 'bin/clang-cl.exe') --target=i686-pc-windows-msvc /nologo /std:c++20 /O2 /EHsc /arch:AVX2 /fp:precise "+ps(OUT/'move-count-fixture.cpp')+' '+ps('/Fe'+str(OUT/'move-count-fixture.exe'))+' '+ps('/Fo'+str(OUT/'move-count-fixture.obj')),
    "if($LASTEXITCODE){throw 'Move-count fixture compilation failed'}",
    '& '+ps(OUT/'move-count-fixture.exe')+' | Tee-Object -FilePath '+ps(OUT/'move-count-result.json'),
    "if($LASTEXITCODE){throw 'Move-count fixture checks failed'}",
]),encoding='utf-8')
result=subprocess.run(['pwsh','-NoProfile','-File',str(script)],cwd=ROOT,capture_output=True,timeout=90)
(OUT/'run.log').write_bytes(result.stdout+result.stderr)
print((result.stdout+result.stderr).decode('utf-8',errors='replace'))
raise SystemExit(result.returncode)
