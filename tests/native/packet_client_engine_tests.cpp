// Execute the production transport hook inside the exact copied engine's
// original NET_QueuePacket -> NET_GetPacket receive path, without DllMain.
#include <metahook.h>
#include <netadr.h>
#include <bcrypt.h>
#include "../../native/client/packet_entities.hpp"
#include "goldcraft/packet_entities.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
struct Buffer { const char* name; std::uint16_t flags,padding; unsigned char* data; int maxsize,cursize; };
unsigned char* base = nullptr;
std::uint64_t engine_crc = 0x6ef7192cd8254e2dULL;
int (__cdecl* hook_get_long)(unsigned char*,int,int*) = nullptr;
using ChannelSetup = void (__cdecl*)(int,void*,netadr_t,int,void*,int (*)(void*));
ChannelSetup hook_setup = nullptr;
int (__cdecl* hook_queue)(int) = nullptr;
struct HookRecord { void* address; unsigned cut; std::array<unsigned char,8> saved; void* trampoline; bool active; };
std::array<HookRecord,3> hooks{};
bool fail_setup_hook = false,fail_queue_hook = false;
netadr_t peer{};
struct Datagram { Bytes bytes; netadr_t source; };
std::vector<Datagram> queued;
std::size_t receive_cursor=0;
void overwrite(void* address, std::span<const std::uint8_t> bytes) {
    DWORD old = 0,restored = 0;
    assert(VirtualProtect(address,bytes.size(),PAGE_EXECUTE_READWRITE,&old));
    std::memcpy(address,bytes.data(),bytes.size());
    assert(VirtualProtect(address,bytes.size(),old,&restored));
    assert(FlushInstructionCache(GetCurrentProcess(),address,bytes.size()));
}
void jump(void* address,const void* target) {
    std::array<std::uint8_t,5> bytes{0xe9};
    const auto distance = std::uint32_t(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(address)-5);
    std::memcpy(bytes.data()+1,&distance,4); overwrite(address,bytes);
}
void* engine_base() { return base; }
mh_gamesymbol_status_t crc(void*,std::uint64_t* value) { *value=engine_crc;return MH_GAMESYMBOL_OK; }
mh_gamesymbol_status_t resolve(void*,const char* name,mh_gamesymbol_kind_t kind,void** result) {
    struct Symbol { const char* name;unsigned rva;mh_gamesymbol_kind_t kind; };
    constexpr Symbol symbols[]{
        {"GoldCraftPacket_parse",0x19f0d0,MH_GAMESYMBOL_KIND_FUNCTION},
        {"GoldCraftPacket_publish",0x19fd20,MH_GAMESYMBOL_KIND_FUNCTION},
        {"GoldCraftPacket_get_long",0x1de840,MH_GAMESYMBOL_KIND_FUNCTION},
        {"GoldCraftPacket_channel_setup",0x1dca20,MH_GAMESYMBOL_KIND_FUNCTION},
        {"GoldCraftPacket_queue_packet",0x1df890,MH_GAMESYMBOL_KIND_FUNCTION},
        {"GoldCraftPacket_network_init",0x1def00,MH_GAMESYMBOL_KIND_FUNCTION},
        {"GoldCraftPacket_in_message",0x1225fa0,MH_GAMESYMBOL_KIND_GLOBAL},
        {"GoldCraftPacket_net_message",0x1245fe0,MH_GAMESYMBOL_KIND_GLOBAL},
        {"GoldCraftPacket_in_from",0x1225f80,MH_GAMESYMBOL_KIND_GLOBAL},
        {"GoldCraftPacket_client_channel",0x1408de8,MH_GAMESYMBOL_KIND_GLOBAL}};
    for (const auto& s:symbols) if (!std::strcmp(name,s.name)) {
        assert(kind==s.kind);*result=base+s.rva;return MH_GAMESYMBOL_OK;
    }
    return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
}
const char* status(mh_gamesymbol_status_t) { return "fixture lookup"; }
hook_t* inline_hook(void* address,void* replacement,void** original) {
    assert(address==base+0x1de840 || address==base+0x1dca20 || address==base+0x1df890);
    if(address==base+0x1dca20 && fail_setup_hook)return nullptr;
    if(address==base+0x1df890 && fail_queue_hook)return nullptr;
    auto& record=hooks[address==base+0x1de840?0:address==base+0x1dca20?1:2];assert(!record.active);
    record.address=address;record.cut=address==base+0x1de840?7:address==base+0x1dca20?6:8;
    std::memcpy(record.saved.data(),address,record.cut);
    // Complete original instruction bytes, with no relative operands. Keep
    // the original native split parser available for normal packet calls.
    auto trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(trampoline);std::memcpy(trampoline,address,record.cut);
    jump(trampoline+record.cut,static_cast<unsigned char*>(address)+record.cut);
    record.trampoline=trampoline;record.active=true;*original=trampoline;
    if(address==base+0x1de840)hook_get_long=reinterpret_cast<decltype(hook_get_long)>(replacement);
    else if(address==base+0x1dca20)hook_setup=reinterpret_cast<ChannelSetup>(replacement);
    else hook_queue=reinterpret_cast<decltype(hook_queue)>(replacement);
    jump(address,replacement);return reinterpret_cast<hook_t*>(&record);
}
BOOL unhook(hook_t* handle) {
    auto& record=*reinterpret_cast<HookRecord*>(handle);assert(record.active);
    overwrite(record.address,std::span(record.saved).first(record.cut));
    assert(VirtualFree(record.trampoline,0,MEM_RELEASE));record.active=false;
    if(record.address==base+0x1de840)hook_get_long=nullptr;
    else if(record.address==base+0x1dca20)hook_setup=nullptr;
    else hook_queue=nullptr;
    return TRUE;
}
void* __cdecl copy(void* dest,const void* src,std::size_t size) { return std::memcpy(dest,src,size); }
void* __cdecl clear(void* dest,int value,std::size_t size) { return std::memset(dest,value,size); }
void __cdecl print(const char*,...) {}
int __stdcall last_error() { return 10035; } // WSAEWOULDBLOCK.
int __stdcall receive(int,char* data,int size,int,void* address,int* address_size) {
    if(receive_cursor==queued.size())return -1;
    const auto& datagram=queued[receive_cursor++];
    assert(int(datagram.bytes.size())<size);std::memcpy(data,datagram.bytes.data(),datagram.bytes.size());
    assert(*address_size>=16);std::memset(address,0,16);
    auto p=static_cast<unsigned char*>(address);p[0]=2;
    std::memcpy(p+2,&datagram.source.port,2);std::memcpy(p+4,datagram.source.ip,4);
    return int(datagram.bytes.size());
}
bool poll() { return reinterpret_cast<int (__cdecl*)(int)>(base+0x1dea20)(0)!=0; }
bool packet(Bytes bytes,const netadr_t& from=peer) {
    queued={{std::move(bytes),from}};receive_cursor=0;return poll();
}
Bytes native_fragment(std::span<const std::uint8_t> bytes,unsigned index,unsigned count,unsigned sequence) {
    Bytes wire(9+bytes.size());
    goldcraft::packet_entities::write(wire,0,0xfffffffeu,4);
    goldcraft::packet_entities::write(wire,4,sequence,4);wire[8]=std::uint8_t(index*16+count);
    std::memcpy(wire.data()+9,bytes.data(),bytes.size());return wire;
}
Bytes read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};
}
}

int main(int argc,char** argv) {
    assert(argc==2 || argc==3);
    const auto bytes=read(argv[1]);
    std::array<unsigned char,32> hash{};
    assert(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
                      ULONG(bytes.size()),hash.data(),ULONG(hash.size()))==0);
    constexpr unsigned char expected[]{0x9b,0xa9,0xa2,0xdb,0x5e,0x07,0x59,0x8f,0xd5,0x9a,0xfa,0x35,0x50,0x7a,0x98,0xc8,
        0x61,0x62,0xe4,0xe1,0x5b,0x38,0x35,0x17,0x7b,0x78,0xc1,0x18,0x42,0xcd,0x22,0x95};
    assert(std::equal(hash.begin(),hash.end(),expected));
    const auto image=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);assert(image);
    base=reinterpret_cast<unsigned char*>(image);
    metahook_api_t api{};api.GetEngineBase=engine_base;api.GetModuleCRC64=crc;
    api.ResolveGameSymbol=resolve;api.GetGameSymbolStatusString=status;api.InlineHook=inline_hook;
    api.UnHook=unhook;
    engine_crc=1;assert(!goldcraft::packet_client::install(&api));assert(!hook_get_long);
    engine_crc=0x6ef7192cd8254e2dULL;
    const unsigned char bad=0x02,good=0x04;
    overwrite(base+0x19f410,{&bad,1});
    assert(!goldcraft::packet_client::install(&api));assert(!hook_get_long);
    overwrite(base+0x19f410,{&good,1});
    fail_setup_hook=true;assert(!goldcraft::packet_client::install(&api));
    assert(!hook_get_long && !hook_setup && !hooks[0].active);
    fail_setup_hook=false;
    fail_queue_hook=true;assert(!goldcraft::packet_client::install(&api));
    assert(!hook_get_long && !hook_setup && !hook_queue && !hooks[0].active && !hooks[1].active);
    fail_queue_hook=false;
    assert(goldcraft::packet_client::install(&api));assert(hook_get_long && hook_setup && hook_queue);
    jump(base+0x25d3b9,reinterpret_cast<void*>(receive));
    jump(base+0x25d3e3,reinterpret_cast<void*>(last_error));
    jump(base+0x1bafc0,reinterpret_cast<void*>(copy));
    jump(base+0x2ad9ce,reinterpret_cast<void*>(copy));
    jump(base+0x1bb020,reinterpret_cast<void*>(clear));
    jump(base+0x1bc070,reinterpret_cast<void*>(print));
    auto& incoming=*reinterpret_cast<Buffer*>(base+0x1225fa0);
    auto& message=*reinterpret_cast<Buffer*>(base+0x1245fe0);
    incoming.data=base+0x1225fc0;incoming.maxsize=65536;incoming.cursize=0;
    message.data=base+0x1235fe0;message.maxsize=65536;message.cursize=0;
    *reinterpret_cast<int*>(base+0x4bb8b8)=1;*reinterpret_cast<int*>(base+0x4bb8bc)=0;
    *reinterpret_cast<int*>(base+0x4bb8b4)=0;*reinterpret_cast<float*>(base+0x31ccd0)=0;
    *reinterpret_cast<float*>(base+0x31bd0c)=0;
    peer.type=NA_IP;peer.ip[0]=127;peer.ip[3]=1;peer.port=0x8769;
    auto setup=[&](void* channel=base+0x1408de8){hook_setup(0,channel,peer,0,nullptr,nullptr);};
    setup();
    std::memcpy(incoming.data+65536,"TAIL",4);
    unsigned tests=0;
    assert(packet(Bytes{7,8,9,10}));assert(message.cursize==4 && message.data[3]==10);++tests;
    Bytes native(6009);for(unsigned i=0;i<native.size();++i)native[i]=std::uint8_t(i*31);
    *reinterpret_cast<int*>(base+0x12247c0)=-1;
    for(unsigned i=0;i<5;++i) {
        const auto offset=i*1391, size=std::min<std::size_t>(1391,native.size()-offset);
        assert(packet(native_fragment(std::span(native).subspan(offset,size),i,5,23))==(i==4));
    }
    assert(message.cursize==6009 && !std::memcmp(message.data,native.data(),native.size()));++tests;
    for(const auto size:{7046u,16384u,65536u}) {
        Bytes data(size);for(unsigned i=0;i<size;++i)data[i]=std::uint8_t(i*17+size);
        const auto count=(size+goldcraft::packet_entities::fragment_bytes-1)/goldcraft::packet_entities::fragment_bytes;
        const auto sequence=size;
        for(std::size_t i=count;i--;) {
            std::array<std::uint8_t,1400> wire{};
            const auto used=goldcraft::packet_entities::encode(data,sequence,i,wire);assert(used);
            assert(packet(Bytes(wire.begin(),wire.begin()+used))==(i==0));
        }
        assert(message.cursize==int(size) && !std::memcmp(message.data,data.data(),size));
        assert(!std::memcmp(incoming.data+65536,"TAIL",4));assert(message.data==base+0x1235fe0 && message.maxsize==65536);++tests;
    }
    goldcraft::packet_client::reset();
    Bytes data(7046,0x3c);std::array<std::uint8_t,1400> wire{};
    auto size=goldcraft::packet_entities::encode(data,1,0,wire);
    assert(!packet(Bytes(wire.begin(),wire.begin()+size)));
    assert(!packet(Bytes(wire.begin(),wire.begin()+size))); // Duplicate is incomplete.
    incoming.maxsize=6010;
    size=goldcraft::packet_entities::encode(data,1,1,wire);assert(!packet(Bytes(wire.begin(),wire.begin()+size)));
    incoming.maxsize=65536;goldcraft::packet_client::reset();++tests;
    std::array<unsigned char,32> arbitrary{};int out=16;
    size=goldcraft::packet_entities::encode(Bytes(1,8),2,0,wire);
    std::memcpy(arbitrary.data(),wire.data(),size);
    assert(!hook_get_long(arbitrary.data(),int(size),&out) && out==16);++tests;
    auto extension=[](const Bytes& bytes,unsigned sequence,unsigned index) {
        std::array<std::uint8_t,1400> buffer{};
        const auto used=goldcraft::packet_entities::encode(bytes,sequence,index,buffer);assert(used);
        return Bytes(buffer.begin(),buffer.begin()+used);
    };
    setup();
    auto unrelated=peer;unrelated.port=0x8877;
    assert(!packet(extension(data,1000000,0),unrelated));
    assert(!packet(extension(data,100000,0)));
    unrelated=peer;unrelated.ip[3]=2;
    assert(!packet(extension(data,1000000,1),unrelated));
    const auto fragments=(data.size()+goldcraft::packet_entities::fragment_bytes-1)/goldcraft::packet_entities::fragment_bytes;
    for(unsigned i=1;i<fragments;++i)assert(packet(extension(data,100000,i))==(i+1==fragments));
    assert(message.cursize==int(data.size()) && !std::memcmp(message.data,data.data(),data.size()));++tests;
    // Same endpoint, restarted server split sequence: actual channel setup
    // must permit1 after100000 without waiting for the expiry clock.
    assert(!packet(extension(data,1,0)));setup();
    for(unsigned i=0;i<fragments;++i)assert(packet(extension(data,1,i))==(i+1==fragments));
    assert(message.cursize==int(data.size()));++tests;
    Bytes large(65536);for(unsigned i=0;i<large.size();++i)large[i]=std::uint8_t(i*13);
    auto burst=[&](const Bytes& bytes,unsigned sequence) {
        queued.clear();receive_cursor=0;
        const auto count=(bytes.size()+goldcraft::packet_entities::fragment_bytes-1)/goldcraft::packet_entities::fragment_bytes;
        for(unsigned i=count;i--;)queued.push_back({extension(bytes,sequence,i),peer});
    };
    setup();burst(large,1);assert(queued.size()==48 && poll() && receive_cursor==48);
    assert(message.cursize==65536 && !std::memcmp(message.data,large.data(),large.size()));++tests;
    // Missing middle fragment drains to socket-empty, then completes when
    // the real remaining fragment arrives in a later poll.
    burst(large,2);queued.erase(queued.begin()+20);assert(!poll() && receive_cursor==47);
    assert(packet(extension(large,2,27)) && message.cursize==65536);++tests;
    // A duplicate flood is bounded:96 receive attempts in one poll.
    queued.clear();receive_cursor=0;
    for(unsigned i=0;i<97;++i)queued.push_back({extension(large,3,0),peer});
    for(unsigned i=1;i<48;++i)queued.push_back({extension(large,3,i),peer});
    assert(!poll() && receive_cursor==96);assert(poll() && receive_cursor==144);++tests;
    // A native packet encountered while draining is returned immediately;
    // pending extensions resume on the following engine poll.
    burst(large,4);queued.insert(queued.begin()+1,Datagram{{42,43,44,45},peer});
    assert(poll() && receive_cursor==2 && message.cursize==4 && message.data[0]==42);
    assert(poll() && receive_cursor==49 && message.cursize==65536);++tests;
    // Ordinary native split bursts keep the exact original one-poll pace.
    *reinterpret_cast<int*>(base+0x12247c0)=-1;
    queued.clear();receive_cursor=0;
    for(unsigned i=0;i<5;++i) {
        const auto offset=i*1391,size=std::min<std::size_t>(1391,native.size()-offset);
        queued.push_back({native_fragment(std::span(native).subspan(offset,size),i,5,24),peer});
    }
    assert(!poll() && receive_cursor==1);
    for(unsigned i=1;i<5;++i)assert(poll()==(i==4) && receive_cursor==i+1);
    assert(message.cursize==6009);++tests;
    assert(!packet(extension(data,5,0)));
    std::array<unsigned char,0x34c0> other_channel{};setup(other_channel.data());
    for(unsigned i=1;i<fragments;++i)assert(packet(extension(data,5,i))==(i+1==fragments));
    assert(message.cursize==int(data.size()));++tests;
    auto process=[&]{return reinterpret_cast<int (__cdecl*)(void*)>(base+0x1dc230)(base+0x1408de8);};
    Bytes complete(65536);for(unsigned i=8;i<complete.size();++i)complete[i]=std::uint8_t(i*37);
    goldcraft::packet_entities::write(complete,0,201,4);goldcraft::packet_entities::write(complete,4,17,4);
    const auto plaintext=complete;
    // Construct the inverse of original COM_UnMunge2 using the verified
    // table and matching ReHLDS COM_Munge2 formula. The adjacent original
    // routine is COM_UnMunge (table1), not a synthetic packet encoder.
    constexpr std::uint8_t table2[]{0x05,0x61,0x7a,0xed,0x1b,0xca,0x0d,0x9b,0x4a,0xf1,0x64,0xc7,0xb5,0x8e,0xdf,0xa0};
    assert(!std::memcmp(base+0x31a67c,table2,sizeof(table2)));
    for(unsigned at=8;at+4<=complete.size();at+=4) {
        std::uint32_t value{};std::memcpy(&value,complete.data()+at,4);value^=~201u;
        value=(value>>24)|((value>>8)&0xff00)|((value<<8)&0xff0000)|(value<<24);
        auto octets=reinterpret_cast<std::uint8_t*>(&value);
        for(unsigned j=0;j<4;++j)octets[j]^=std::uint8_t(0xa5|(j<<j)|j|table2[((at-8)/4+j)&15]);
        value^=201;std::memcpy(complete.data()+at,&value,4);
    }
    setup();burst(complete,1);assert(poll() && message.cursize==65536);
    assert(process()==1 && message.cursize==65536 && !std::memcmp(message.data,plaintext.data(),plaintext.size()));
    assert(*reinterpret_cast<int*>(base+0x1408de8+56)==201);++tests;
    unsigned replay=0;
    if(argc==3) {
        // Optional actual server *.datagrams stream: uint16 length + exact
        // wire bytes repeatedly. *.netchan is the original complete payload.
        for(const auto& entry:std::filesystem::directory_iterator(argv[2])) {
            if(entry.path().extension()!=".datagrams")continue;
            setup();const auto wire_data=read(entry.path());
            std::size_t cursor=0;unsigned complete=0;
            while(cursor<wire_data.size()) {
                assert(cursor+2<=wire_data.size());const auto length=wire_data[cursor]|wire_data[cursor+1]*256;cursor+=2;
                assert(length && cursor+length<=wire_data.size());
                complete+=packet(Bytes(wire_data.begin()+cursor,wire_data.begin()+cursor+length));cursor+=length;
            }
            auto target=entry.path();target.replace_extension(".netchan");const auto expected_message=read(target);
            assert(complete==1 && message.cursize==int(expected_message.size()) &&
                   !std::memcmp(message.data,expected_message.data(),expected_message.size()));++replay;
            assert(process()==1);
            target.replace_extension(".payload");const auto payload=read(target);
            assert(message.cursize==int(expected_message.size()));
            if(expected_message.size()==16 && payload.size()>6010) {
                for(int i=8;i<16;++i)assert(message.data[i]==1); // Original oversize native fallback NOPs.
            } else {
                const auto reliable=expected_message.size()-8-payload.size();assert(reliable==0 || reliable==128);
                assert(!std::memcmp(message.data+8+reliable,payload.data(),payload.size()));
                if(reliable)for(unsigned i=0;i<128;++i)assert(message.data[8+i]==std::uint8_t(i+19));
            }
            std::uint32_t sequence{};std::memcpy(&sequence,expected_message.data(),4);sequence&=0x3fffffff;
            assert(*reinterpret_cast<std::uint32_t*>(base+0x1408de8+56)==sequence);
        }
        assert(replay);
    }
    std::ostringstream state;goldcraft::packet_client::write_status(state);
    std::cout<<"{\"actualEngineReceive\":true,\"productionHook\":true,\"wrongIdentityAndChangedBoundsRefused\":true,"
        "\"native6009Preserved\":true,\"extended65536\":true,\"endpointIsolation\":true,\"sameAddressReconnect\":true,"
        "\"hookFailureRollback\":true,\"burst48SinglePoll\":true,\"boundedReceiveDrain\":true,"
        "\"actualNetchanProcess65536\":true,\"serverNetchanUnmungeCompared\":true,"
        "\"receiveChecks\":"<<tests<<",\"serverReplays\":"<<replay<<","<<state.str()<<",\"passed\":true}\n";
    FreeLibrary(image);
}
