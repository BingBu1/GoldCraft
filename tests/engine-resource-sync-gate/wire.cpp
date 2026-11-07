// SPDX-License-Identifier: GPL-3.0-or-later
// Compiles the actual GC_WriteResourceChunk body extracted at configure time.
#include "goldcraft/precache_protocol.hpp"
#include "goldcraft/resource_sync.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
using byte=unsigned char;
static unsigned checks{};
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x <<'\n';std::abort();}}while(0)
constexpr unsigned RES_FATALIFMISSING=1,RES_WASMISSING=2,RES_CUSTOM=4;
constexpr int svc_resourcerequest=45,svc_resourcelocation=56,svc_resourcelist=43;
struct client_t{bool sync{};};
#include "resource_sync.h"
struct resource_t {char szFileName[64]{};unsigned type{},nIndex{},nDownloadSize{},ucFlags{};byte rgucMD5_hash[16]{},rguc_reserved[32]{};};
struct sizebuf_t{std::vector<byte> bytes;std::size_t bits{};};
static struct{std::vector<resource_t> resources;}gc_precache;
static struct{unsigned spawncount=0x12345678;}g_psvs;
static struct{const char* string="http://127.0.0.1:28080/goldcraft/";}sv_downloadurl;
static client_t* manifestClient{};
static unsigned manifestFirst{};
static sizebuf_t* writing{};
static std::vector<goldcraft::resource_sync::Entry> entries;
static constexpr std::uint64_t generation=0x1122334455667788ull;
bool GC_ResourceSyncClient(const client_t* client){return GC_ResourceSyncEnabled() && client->sync;}
std::uint64_t GC_ResourceSyncGeneration(){return generation;}
const goldcraft::resource_sync::Entry* GC_ResourceSyncEntry(std::size_t i){return &entries.at(i);}
static std::size_t Q_strlen(const char* text){return std::strlen(text);}
static int Q_memcmp(const void* a,const void* b,std::size_t size){return std::memcmp(a,b,size);}
static void write(sizebuf_t* msg,unsigned value,unsigned count){
    if(count<32)value=(std::min)(value,(1u<<count)-1);
    for(unsigned i=0;i<count;++i,++msg->bits){if(msg->bits/8>=msg->bytes.size())msg->bytes.push_back(0);msg->bytes[msg->bits/8]|=byte(((value>>i)&1)<<(msg->bits%8));}
}
static void MSG_WriteByte(sizebuf_t* m,int value){write(m,unsigned(value),8);}
static void MSG_WriteLong(sizebuf_t* m,unsigned value){write(m,value,32);}
static void MSG_WriteString(sizebuf_t* m,const char* text){do{write(m,byte(*text),8);}while(*text++);}
static void MSG_StartBitWriting(sizebuf_t* m){writing=m;}
static void MSG_WriteBits(unsigned value,int count){write(writing,value,unsigned(count));}
static void MSG_WriteBitString(const char* text){MSG_WriteString(writing,text);}
static void MSG_WriteBitData(void* data,int size){for(int i=0;i<size;++i)write(writing,static_cast<byte*>(data)[i],8);}
static void MSG_EndBitWriting(sizebuf_t*){writing=nullptr;}
static void SV_SendConsistencyList(sizebuf_t*){MSG_WriteBits(0x35,6);}
#include "resource_sync_wire_adapter.inc"
struct Reader {
    const sizebuf_t& data;std::size_t bit{};
    unsigned Read(unsigned count){CHECK(bit+count<=data.bits);unsigned value=0;for(unsigned i=0;i<count;++i,++bit)value|=unsigned((data.bytes[bit/8]>>(bit%8))&1)<<i;return value;}
    std::string Text(){std::string text;for(unsigned c;(c=Read(8))!=0;)text+=char(c);return text;}
};
static void validate(bool sync,unsigned first){
    client_t client{sync};manifestClient=&client;manifestFirst=first;sizebuf_t msg;CHECK(GC_WriteResourceChunk(&msg));CHECK(msg.bytes.size()<65536);
    sync = sync && GC_ResourceSyncEnabled();
    Reader r{msg};if(!first){CHECK(r.Read(8)==45);CHECK(r.Read(32)==g_psvs.spawncount);CHECK(r.Read(32)==0);CHECK(r.Read(8)==56);CHECK(r.Text()==sv_downloadurl.string);}
    CHECK(r.Read(8)==43);CHECK(r.Read(12)==0xfff);CHECK(r.Read(32)==0x31504347);CHECK(r.Read(8)==(sync?4u:3u));
    CHECK(r.Read(32)==gc_precache.resources.size());CHECK(r.Read(32)==first);
    const auto count=(std::min)(128u,unsigned(gc_precache.resources.size())-first);CHECK(r.Read(16)==count);
    if(sync){CHECK(r.Read(32)==0x55667788);CHECK(r.Read(32)==0x11223344);}
    for(unsigned i=first;i<first+count;++i){const auto& source=gc_precache.resources[i];const auto& expected=entries[i];
        CHECK(r.Read(4)==source.type);CHECK(r.Text()==source.szFileName);CHECK(r.Read(32)==source.nIndex);
        CHECK(r.Read(24)==(std::min)(source.nDownloadSize,0xffffffu));CHECK(r.Read(3)==(source.ucFlags&7));
        if(source.ucFlags&4)for(auto c:source.rgucMD5_hash)CHECK(r.Read(8)==c);
        const bool reserved=source.rguc_reserved[0]!=0;CHECK(r.Read(1)==reserved);if(reserved)for(auto c:source.rguc_reserved)CHECK(r.Read(8)==c);
        if(sync){CHECK(r.Read(1)==expected.required);if(expected.required){CHECK(r.Read(32)==expected.size);for(auto c:expected.sha256)CHECK(r.Read(8)==c);}}
    }
    if(first+count==gc_precache.resources.size())CHECK(r.Read(6)==0x35);
    CHECK(r.bit==msg.bits);
}
int main(){
    sizebuf_t empty;CHECK(!GC_WriteResourceChunk(&empty)&&empty.bytes.empty());
    validate(false,0);validate(true,0);
    for(unsigned i=0;i<129;++i){resource_t r;auto path=std::string("models/")+std::string(52,char('a'+i%26))+".mdl";CHECK(path.size()==63);std::strcpy(r.szFileName,path.c_str());r.type=i%6;r.nIndex=65536+i;r.nDownloadSize=22309736;r.ucFlags=i%8;
        for(unsigned j=0;j<16;++j)r.rgucMD5_hash[j]=byte(j+1);if(i%2)for(unsigned j=0;j<32;++j)r.rguc_reserved[j]=byte(j+1);
        goldcraft::resource_sync::Entry e{!(r.ucFlags&4)&&r.type!=3,22309736,{}};for(unsigned j=0;j<32;++j)e.sha256[j]=byte(i+j);
        gc_precache.resources.push_back(r);entries.push_back(e);
    }
    for(bool sync:{false,true}){validate(sync,0);validate(sync,128);}
    std::cout<<"resource_sync production wire checks="<<checks<<" passed\n";
}
