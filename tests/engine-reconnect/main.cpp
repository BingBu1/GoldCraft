// SPDX-License-Identifier: GPL-3.0-or-later
// Compile with the actual SV_New_f and SV_CheckTimeouts bodies extracted by
// Test-EngineReconnect.py. Engine/network/game-DLL edges are controlled here.
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define REHLDS_FIXES
#define TRUE 1
#define FALSE 0
#define NET_MAX_PAYLOAD 65536
#define SIZEBUF_CHECK_OVERFLOW 1
#define Q_memset std::memset
#define Q_snprintf std::snprintf
struct edict_t {};
struct sizebuf_t { const char* buffername{}; unsigned char* data{}; int maxsize{}, cursize{}, flags{}; };
struct netchan_t { sizebuf_t message; double last_received{}; int remote_address{}; };
struct client_t
{
    int active{}, connected{}, spawned{}, fully_connected{}, fakeclient{}, hasusrmsgs{}, m_bSentNewResponse{};
    int m_sendrescount{};
    double connection_started{};
    netchan_t netchan;
    sizebuf_t datagram;
    edict_t* edict{};
    char name[64]{"test"};
};
struct GameClient
{
    double deadline{};
    int cleared{}, setups{};
    double GetReconnectDeadline() { return deadline; }
    void SetReconnectDeadline(double value) { deadline = value; if (!value) ++cleared; }
    void SetupLocalGameTime() { ++setups; }
};
struct Server { int maxclients{}; client_t* clients{}; } g_psvs;
std::array<client_t,32> clients;
std::array<GameClient,32> gameClients;
std::array<GameClient*,32> g_GameClients;
client_t* host_client{};
struct Cvar { float value{}; } sv_timeout{300}, sv_reconnect_timeout{30};
double realtime{};
enum { src_command, src_client };
int cmd_source{}, sv_gpUserMsgs{}, svc_stufftext{};
bool gameAccepts{};
int drops{}, fragments{}, newResponses{}, gameConnectCalls{}, failures{}, checks{};
std::string dropReason;
struct EntityInterface
{
    void (*pfnClientDisconnect)(edict_t*);
    int (*pfnClientConnect)(edict_t*,const char*,const char*,char*);
} gEntityInterface;
struct RateLimiter { void ClientConnected(int) {} } g_DlFileRateLimiter;
void SZ_Clear(sizebuf_t*) {}
void Netchan_Clear(netchan_t*) {}
void SV_SendServerinfo(sizebuf_t*,client_t*) { ++newResponses; }
void SV_SendUserReg(sizebuf_t*,int) {}
const char* NET_AdrToString(int) { return "127.0.0.1"; }
const char* Info_Serverinfo() { return ""; }
void MSG_WriteByte(sizebuf_t*,int) {}
void MSG_WriteString(sizebuf_t*,const char*) {}
void SV_FullClientUpdate(client_t*,sizebuf_t*) {}
void Netchan_CreateFragments(int,netchan_t*,sizebuf_t*) { ++fragments; }
void Netchan_FragSend(netchan_t*) {}
void SV_ClientPrintf(const char*,...) {}
void SV_BroadcastPrintf(const char*,...) {}
void Con_DPrintf(const char*,...) {}
const char* va(const char* format,...)
{
    static char text[256]; va_list args; va_start(args,format);
    std::vsnprintf(text,sizeof(text),format,args); va_end(args); return text;
}
void SV_DropClient(client_t* client,int,const char* reason,...)
{
    ++drops; dropReason=reason;
    client->active=client->connected=client->spawned=FALSE;
}

#include "actual_reconnect.inc"

void Check(bool value,const char* name)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %d %s\n",checks,name); }
}
void Reset(int slot=0)
{
    clients={}; gameClients={};
    for(unsigned i=0;i<32;++i)g_GameClients[i]=&gameClients[i];
    g_psvs={32,clients.data()}; host_client=&clients[slot];
    host_client->connected=TRUE; host_client->netchan.last_received=100;
    gameClients[slot].deadline=100; realtime=100;
    cmd_source=src_client; gameAccepts=true;
    drops=fragments=newResponses=gameConnectCalls=0; dropReason.clear();
    sv_timeout.value=300; sv_reconnect_timeout.value=30;
    gEntityInterface.pfnClientDisconnect=[](edict_t*){};
    gEntityInterface.pfnClientConnect=[](edict_t*,const char*,const char*,char*)
    { ++gameConnectCalls; return gameAccepts ? TRUE : FALSE; };
}
int main()
{
    for(int slot: {0,31})
    {
        Reset(slot); SV_New_f();
        Check(gameClients[slot].deadline==0 && gameClients[slot].cleared==1,
              "accepted new clears reconnect deadline before loading");
        Check(fragments==1 && host_client->m_bSentNewResponse && gameConnectCalls==1,
              "normal sign-on still sent");
        Check(gameClients[slot ? 0:31].cleared==0,"other player slot untouched");
        realtime=135; host_client->netchan.last_received=135; SV_CheckTimeouts();
        Check(drops==0,"active download beyond 30 seconds is retained");
    }
    // Independent control: once sign-on has satisfied its reconnect deadline,
    // missing netchan traffic must still time out. Do not reuse the previous
    // download case: the old implementation has already dropped that client.
    Reset(); gameClients[0].deadline=0; realtime=500; SV_CheckTimeouts();
    Check(drops==1 && dropReason=="Timed out","normal inactivity timeout still applies");
    Reset(); realtime=131; host_client->netchan.last_received=131; SV_CheckTimeouts();
    Check(drops==1 && dropReason=="Failed to reconnect after level change",
          "netchan traffic without new does not extend reconnect deadline");
    Reset(); realtime=130; SV_CheckTimeouts();
    Check(drops==0,"deadline equality retains original strict-greater condition");
    Reset(); cmd_source=src_command; SV_New_f();
    Check(gameClients[0].cleared==0 && fragments==0,"console new cannot clear client deadline");
    Reset(); host_client->hasusrmsgs=host_client->m_bSentNewResponse=TRUE; SV_New_f();
    Check(gameClients[0].cleared==0 && fragments==0,"duplicate new remains rejected");
    Reset(); host_client->spawned=TRUE; SV_New_f();
    Check(gameClients[0].cleared==0 && fragments==0,"inconsistent inactive spawned state remains rejected");
    Reset(); gameAccepts=false; SV_New_f();
    Check(gameClients[0].cleared==0 && drops==1 && fragments==0,
          "game DLL rejection does not qualify as accepted sign-on");
    Reset(); host_client->fully_connected=TRUE; realtime=131; SV_CheckTimeouts();
    Check(gameClients[0].deadline==0 && drops==0,"existing completed reconnect fallback remains valid");
    Reset(); host_client->fakeclient=TRUE; realtime=500; SV_CheckTimeouts();
    Check(drops==0,"fake clients stay outside network timeout policy");
    Reset(); sv_reconnect_timeout.value=0; realtime=131; SV_CheckTimeouts();
    Check(drops==0,"existing zero timeout option is unchanged");
    std::printf("checks=%d failures=%d\n",checks,failures);
    return failures ? 1 : 0;
}
