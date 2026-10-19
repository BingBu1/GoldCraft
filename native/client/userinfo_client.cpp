#include <metahook.h>
#include <cvardef.h>
#include "userinfo_client.hpp"
#include "goldcraft/userinfo.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

extern cl_enginefunc_t gEngfuncs;
namespace goldcraft::client_userinfo {
namespace {
metahook_api_t* api{};
bool installed = false, syncing = false;
std::string failure;
userinfo::Storage local;
std::array<userinfo::Storage,32> players{};
char *legacy = nullptr, *roster = nullptr;
constexpr std::uintptr_t player_stride = 592;
using Set = void(__cdecl*)(char*,const char*,const char*,int);
using Get = const char*(__cdecl*)(const char*,const char*);
using Remove = void(__cdecl*)(char*,const char*);
Set original_set{}; Get original_get{}; Remove original_remove{};
std::array<hook_t*,3> hooks{};
struct Operand { const char* name; unsigned offset, delta; };
constexpr Operand operands[]{
#include "userinfo_refs.inc"
};
struct Patch { void* address; DWORD before; };
std::vector<Patch> patches;
void* copy_call = nullptr;
std::array<unsigned char,5> original_copy{};
std::deque<std::string> pending;
unsigned revision = 0, queued_revision = 0, rejected = 0, received = 0, sent = 0;

void* resolve(const char* name, mh_gamesymbol_kind_t kind) {
    void* out = nullptr;
    const auto symbol = std::string("GoldCraftUserInfo_") + name;
    if (api->ResolveGameSymbol(api->GetEngineBase(),symbol.c_str(),kind,&out)!=MH_GAMESYMBOL_OK || !out)
        throw std::runtime_error("userinfo symbol unavailable: " + symbol);
    return out;
}
int player_index(const char* info) {
    const auto address = reinterpret_cast<std::uintptr_t>(info);
    const auto first = reinterpret_cast<std::uintptr_t>(roster);
    if (!roster || address < first || address >= first + players.size()*player_stride ||
        (address-first)%player_stride) return -1;
    return int((address-first)/player_stride);
}
bool local_buffer(const char* info) { return info == legacy || info == local.full.data(); }
void publish_local() { local.publish(legacy,userinfo::legacy_capacity-userinfo::identity_reserve); }

void __cdecl set(char* info,const char* key,const char* value,int capacity) {
    if (!local_buffer(info)) { original_set(info,key,value,capacity); return; }
    if (!key || !value) return;
    // Match the stricter dedicated-engine limit, and leave space for the
    // Steam identity added by the authenticated server.
    if (!userinfo::token(key) || !userinfo::token(value) || std::strlen(key)>=127 || std::strlen(value)>=127 ||
        !userinfo::fits(local.full.data(),key,value,userinfo::capacity-userinfo::identity_reserve)) {
        ++rejected;
        gEngfuncs.Con_Printf("GoldCraft userinfo capacity exceeded (1024 bytes including server identity)\n");
        return;
    }
    original_set(local.full.data(),key,value,int(userinfo::capacity));
    publish_local(); ++revision;
}
const char* __cdecl get(const char* info,const char* key) {
    if (local_buffer(info)) return original_get(local.full.data(),key);
    const int slot=player_index(info);
    if (slot>=0 && info[0] && players[slot].full[0]) return original_get(players[slot].full.data(),key);
    return original_get(info,key);
}
void __cdecl remove(char* info,const char* key) {
    if (!local_buffer(info)) { original_remove(info,key); return; }
    original_remove(local.full.data(),key);
    publish_local(); ++revision;
}
char* __cdecl copy_player(char* dest,const char* source,std::size_t count) {
    const int slot=player_index(dest);
    if (slot<0 || count!=255) return dest; // only the verified parser CALL is redirected
    // MSG_ReadString owns a bounded engine buffer. Never let a malformed or
    // oversized roster update retain an identity from a previous occupant.
    const auto* end=static_cast<const char*>(std::memchr(source,0,userinfo::capacity));
    if (!end || !players[slot].assign({source,std::size_t(end-source)})) {
        players[slot] = {}; ++rejected;
    } else ++received;
    players[slot].publish(dest);
    return dest;
}
void queue_sync() {
    pending.clear(); queued_revision=revision;
    userinfo::fields(local.full.data(),[&](auto key,auto value) {
        // Connect already contains the projection. Passwords and server-only
        // identity fields never enter the post-authentication command stream.
        if (key.front()=='*' || key=="password" || !userinfo::value(local.projection.data(),key).empty()) return;
        pending.push_back("setinfo \""+std::string(key)+"\" \""+std::string(value)+"\"\n");
    });
}
void sync() {
    if (!installed) return;
    syncing=true; queue_sync();
}
}

bool install(metahook_api_t* value) {
    if (installed) return true;
    api=value;
    try {
        std::uint64_t crc=0;
        if (!api || api->GetModuleCRC64(api->GetEngineBase(),&crc)!=MH_GAMESYMBOL_OK || crc!=0x6ef7192cd8254e2dULL)
            throw std::runtime_error("userinfo expansion requires the verified HL10210 engine");
        legacy=reinterpret_cast<char*>(api->ReadDWORD(static_cast<char*>(resolve("ref_0",MH_GAMESYMBOL_KIND_PATCH))+1));
        auto* roster_instruction=static_cast<unsigned char*>(resolve("roster",MH_GAMESYMBOL_KIND_PATCH));
        if (roster_instruction[0]!=0x8d || roster_instruction[1]!=0xb7)
            throw std::runtime_error("userinfo roster instruction changed");
        roster=reinterpret_cast<char*>(api->ReadDWORD(roster_instruction+2));
        if (!std::memchr(legacy,0,userinfo::legacy_capacity) || !local.assign(legacy))
            throw std::runtime_error("initial userinfo is invalid");
        for (const auto& operand:operands) {
            auto* address=static_cast<unsigned char*>(resolve(operand.name,MH_GAMESYMBOL_KIND_PATCH))+operand.offset;
            const auto expected=reinterpret_cast<DWORD>(legacy+operand.delta);
            if (api->ReadDWORD(address)!=expected) throw std::runtime_error("userinfo local operand changed");
            patches.push_back({address,expected});
        }
        void* targets[]{resolve("set",MH_GAMESYMBOL_KIND_FUNCTION),resolve("get",MH_GAMESYMBOL_KIND_FUNCTION),resolve("remove",MH_GAMESYMBOL_KIND_FUNCTION)};
        copy_call=resolve("copy",MH_GAMESYMBOL_KIND_PATCH);
        std::memcpy(original_copy.data(),copy_call,original_copy.size());
        if (original_copy[0]!=0xe8) throw std::runtime_error("userinfo copy is not a CALL");
        const void* replacements[]{reinterpret_cast<void*>(set),reinterpret_cast<void*>(get),reinterpret_cast<void*>(remove)};
        void** originals[]{reinterpret_cast<void**>(&original_set),reinterpret_cast<void**>(&original_get),reinterpret_cast<void**>(&original_remove)};
        for (unsigned i=0;i<3;++i) {
            hooks[i]=api->InlineHook(targets[i],const_cast<void*>(replacements[i]),originals[i]);
            if (!hooks[i]) throw std::runtime_error("userinfo hook installation failed");
        }
        for (unsigned i=0;i<patches.size();++i)
            api->WriteDWORD(patches[i].address,reinterpret_cast<DWORD>(local.full.data()+operands[i].delta));
        const auto relative=std::uint32_t(reinterpret_cast<std::uintptr_t>(copy_player)-reinterpret_cast<std::uintptr_t>(copy_call)-5);
        api->WriteDWORD(static_cast<char*>(copy_call)+1,relative);
        publish_local();
        installed=true;failure.clear();
    } catch (const std::exception& error) { shutdown(); failure=error.what(); }
    return installed;
}
void initialize() {
    if (!installed) return;
    gEngfuncs.pfnRegisterVariable("_gcu","1",FCVAR_USERINFO);
    gEngfuncs.Cvar_SetValue("_gcu",1);
    gEngfuncs.pfnAddCommand("gc_userinfo_sync",sync);
}
void frame() {
    if (!installed || !syncing) return;
    const char* supported=gEngfuncs.ServerInfo_ValueForKey("gc_userinfo_capacity");
    if (!supported || std::strcmp(supported,"1024")) return;
    if (revision!=queued_revision) queue_sync();
    // Keep initial synchronization bounded, even with many tiny fields.
    for (unsigned i=0;i<2 && !pending.empty();++i) {
        gEngfuncs.pfnServerCmd(pending.front().c_str()); pending.pop_front(); ++sent;
    }
    if (pending.empty()) syncing=false;
}
void reset() { pending.clear(); syncing=false; players={}; }
void shutdown() {
    if (api) {
        if (copy_call && original_copy[0]==0xe8) api->WriteMemory(copy_call,original_copy.data(),DWORD(original_copy.size()));
        for (const auto& patch:patches) api->WriteDWORD(patch.address,patch.before);
        for (auto& hook:hooks) if (hook) {api->UnHook(hook);hook=nullptr;}
    }
    patches.clear();copy_call=nullptr;installed=false;reset();
}
const char* error() { return failure.c_str(); }
void diagnostics(std::ostream& out) {
    out << "\"userinfo\":{\"installed\":" << installed << ",\"capacity\":" << (installed?userinfo::capacity:userinfo::legacy_capacity)
        << ",\"localBytes\":" << std::strlen(local.full.data()) << ",\"legacyBytes\":" << (legacy?std::strlen(legacy):0)
        << ",\"pending\":" << pending.size() << ",\"sent\":" << sent << ",\"received\":" << received
        << ",\"rejected\":" << rejected << ",\"error\":" << std::quoted(failure) << '}';
}
}
