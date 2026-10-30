// SPDX-License-Identifier: GPL-3.0-or-later
// Executes the unchanged production server with the matched ReGameDLL SDK.
// Engine boundaries are recorded; this is not a substitute for two live clients.
#include "../../native/csol/server/hd_spray.cpp"
#include <iostream>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot=false;
globalvars_t globals{};
globalvars_t* gpGlobals=&globals;
namespace fixture {
unsigned checks{};std::array<edict_t,33> entities{};std::array<int,33> users{};
struct Packet{int to;wire::Bytes bytes;};std::vector<Packet> packets;
std::vector<std::string> args;float allow=1,logos=1;
void Require(bool b){++checks;if(!b)throw std::runtime_error("HD spray server check "+std::to_string(checks));}
void Begin(int destination,int id,const float*,edict_t* e){Require(destination==MSG_ONE&&id==90);packets.push_back({int(e-entities.data()),{}});}
void Byte(int b){packets.back().bytes.push_back(std::uint8_t(b));}
void End(){Require(packets.back().bytes.size()<=192);}
void Join(int id,int user){auto& e=entities[id];e={};e.v.flags=FL_CLIENT;e.v.netname=1;e.pvPrivateData=&e;users[id]=user;}
void Command(int id,std::vector<std::string> a);
bool acknowledge=true;
void Tick(unsigned n=1){for(unsigned i=0;i<n;++i){globals.time+=.03f;CSOL_HDSprayFrame();
    // Send real production acknowledgement commands at the recorded boundary.
    for(int slot=1;slot<=3;++slot){auto& p=peers[slot];if(acknowledge&&p.sending&&p.offset>p.acked)Command(slot,{"ack",std::to_string(p.sending->id),std::to_string(p.offset)});}
}}
void Command(int id,std::vector<std::string> a){args={"csol_hd",a[0],std::to_string(epoch)};args.insert(args.end(),a.begin()+1,a.end());Require(CSOL_HDSprayCommand(&entities[id],"csol_hd"));}
unsigned Upload(int id,const wire::Bytes& b){Command(id,{"begin",std::to_string(b.size()),std::to_string(wire::Checksum(b)),"1024","1024"});unsigned asset=peers[id].upload.id;Require(asset!=0);
    for(size_t at=0;at<b.size();at+=wire::UploadChunk){globals.time+=.01f;Command(id,{"data",std::to_string(asset),std::to_string(at),wire::Base64(std::span(b).subspan(at,std::min(size_t(wire::UploadChunk),b.size()-at)))});}
    Require(peers[id].active&&peers[id].active->id==asset);return asset;
}
void Remote(int id,unsigned asset,unsigned count,unsigned frames=1){
    wire::Transfer incoming;bool sawImage=false;unsigned placements=0;
    for(const auto& packet:packets)if(packet.to==id){wire::Reader r{packet.bytes};auto type=r.U8();Require(r.U32()==epoch);
        if(type==wire::Begin){auto a=r.U32(),n=r.U32(),c=r.U32();r.End();incoming.Begin(a,n,c);}
        else if(type==wire::Data){auto a=r.U32(),o=r.U32();if(incoming.Append(a,o,r.bytes.subspan(r.at))){Require(a==asset);auto animation=wire::DecodeAnimation(incoming.bytes);Require(animation.First().Width==512&&animation.First().Height==512&&animation.frames.size()==frames);if(frames==2)Require(animation.plays==3&&animation.frames[1].duration==250);sawImage=true;}}
        else if(type==wire::Placement){r.U32();Require(r.U32()==asset);r.U32();r.U32();for(int n=0;n<6;++n)r.Float();Require(r.U32()==1024&&r.U32()==1024);Require(r.Float()>0);r.End();++placements;}
    }Require(sawImage&&placements==count);
}
}
int main(){using namespace fixture;try{
    g_engfuncs.pfnRegUserMsg=[](const char*,int n){Require(n==-1);return 90;};
    g_engfuncs.pfnPEntityOfEntIndex=[](int i){return &entities.at(i);};g_engfuncs.pfnIndexOfEdict=[](const edict_t* e){return int(e-entities.data());};
    g_engfuncs.pfnGetPlayerUserId=[](edict_t* e){return users.at(e-entities.data());};
    g_engfuncs.pfnMessageBegin=Begin;g_engfuncs.pfnMessageEnd=End;g_engfuncs.pfnWriteByte=Byte;
    g_engfuncs.pfnCmd_Argc=[](){return int(args.size());};g_engfuncs.pfnCmd_Argv=[](int i){return args.at(i).c_str();};
    // Match the engine's registered names, including unknown-cvar fallback.
    g_engfuncs.pfnCVarGetFloat=[](const char* name){if(!std::strcmp(name,"sv_allowupload"))return allow;if(!std::strcmp(name,"sv_send_logos"))return logos;return 0.0f;};
    Require(g_engfuncs.pfnCVarGetFloat("sv_allow_upload")==0);
    globals.maxClients=3;for(auto& e:entities)e.free=TRUE;entities[0]={};entities[0].v.modelindex=1;
    CSOL_HDSprayActivate();Join(1,101);Join(2,102);Tick(40);Command(1,{"hello",std::to_string(wire::Version)});Command(2,{"hello",std::to_string(wire::Version)});
    csol::Image image{512,512,wire::Bytes(512*512*4,255)};auto data=wire::Encode(image);const auto asset=Upload(1,data);Require(peers[1].active->original.width==1024&&peers[1].active->original.height==1024);
    TraceResult tr{};tr.flFraction=.25f;tr.pHit=&entities[0];tr.vecEndPos=Vector(10,20,30);tr.vecPlaneNormal=Vector(1,0,0);
    Require(CSOL_HDSprayTrace(&entities[1],tr));Require(decals.size()==1&&decals[0].position.x==10);
    packets.clear();acknowledge=false;Tick(100);Require(peers[2].offset==wire::Chunk*8);auto paused=peers[2].offset;Tick(100);Require(peers[2].offset==paused);
    acknowledge=true;Tick(1000);Remote(2,asset,1);
    Join(2,202);Tick(40);Command(2,{"hello",std::to_string(wire::Version)});packets.clear();Tick(1000);Remote(2,asset,1);
    // Late join gets both retained image and authoritative placement.
    Join(3,103);Tick(40);Command(3,{"hello",std::to_string(wire::Version)});packets.clear();Tick(1000);Remote(3,asset,1);
    for(unsigned i=0;i<wire::MaxDecals+3;++i)Require(CSOL_HDSprayTrace(&entities[1],tr));
    Require(decals.size()==wire::MaxDecals&&decals.front().id>1);
    // A replacement occupant does not inherit another user's upload or capability.
    Join(1,201);Tick();Require(!peers[1].active&&!peers[1].capable);Require(!CSOL_HDSprayTrace(&entities[1],tr));
    Command(1,{"hello",std::to_string(wire::Version)});Command(1,{"begin",std::to_string(wire::MaxBytes+1),"1","1024","1024"});Require(!peers[1].upload.id);
    for(auto dimensions:{wire::Dimensions{0,512},wire::Dimensions{32769,512},wire::Dimensions{32768,32768}}){
        globals.time+=4;Command(1,{"begin",std::to_string(data.size()),std::to_string(wire::Checksum(data)),std::to_string(dimensions.width),std::to_string(dimensions.height)});Require(!peers[1].upload.id);
    }
    // Legal source bounds with an inconsistent texture aspect must still fail.
    globals.time+=4;Command(1,{"begin",std::to_string(data.size()),std::to_string(wire::Checksum(data)),"1024","512"});
    auto wrongAspect=peers[1].upload.id;Require(wrongAspect!=0);
    for(size_t at=0;at<data.size();at+=wire::UploadChunk){globals.time+=.01f;Command(1,{"data",std::to_string(wrongAspect),std::to_string(at),wire::Base64(std::span(data).subspan(at,std::min(size_t(wire::UploadChunk),data.size()-at)))});}
    Require(!peers[1].active&&!peers[1].upload.id);
    auto corrupt=data;corrupt.back()=0;globals.time+=4;Command(1,{"begin",std::to_string(corrupt.size()),std::to_string(wire::Checksum(corrupt)),"1024","1024"});
    unsigned pending=peers[1].upload.id;Require(pending!=0);
    for(size_t at=0;at<corrupt.size();at+=wire::UploadChunk){globals.time+=.01f;Command(1,{"data",std::to_string(pending),std::to_string(at),wire::Base64(std::span(corrupt).subspan(at,std::min(size_t(wire::UploadChunk),corrupt.size()-at)))});}
    Require(!peers[1].active&&!peers[1].upload.id);
    globals.time+=4;Command(1,{"begin",std::to_string(data.size()),std::to_string(wire::Checksum(data)),"1024","1024"});pending=peers[1].upload.id;
    Command(1,{"data",std::to_string(pending),"1","AAAA"});Require(!peers[1].upload.id);
    // Existing server upload restrictions are honored.
    allow=0;globals.time+=4;Command(1,{"begin",std::to_string(data.size()),std::to_string(wire::Checksum(data)),"1024","1024"});Require(!peers[1].upload.id);allow=1;
    logos=0;globals.time+=4;Command(1,{"begin",std::to_string(data.size()),std::to_string(wire::Checksum(data)),"1024","1024"});Require(!peers[1].upload.id);logos=1;
    globals.time+=4;Upload(1,data);logos=0;Require(!CSOL_HDSprayTrace(&entities[1],tr));logos=1;
    auto oldEpoch=epoch;CSOL_HDSprayActivate();Require(epoch!=oldEpoch&&decals.empty()&&!peers[2].capable);
    Tick(40);Command(1,{"hello",std::to_string(wire::Version)});Command(2,{"hello",std::to_string(wire::Version)});
    wire::Animation animation{{{image,100},{csol::Image{512,512,wire::Bytes(512*512*4,0)},250}},3};
    const auto animated=wire::EncodeAnimation(animation);const auto animatedId=Upload(1,animated);Require(CSOL_HDSprayTrace(&entities[1],tr));
    const float started=decals.back().started;packets.clear();Tick(1000);Remote(2,animatedId,1,2);
    Join(3,303);Tick(40);Command(3,{"hello",std::to_string(wire::Version)});packets.clear();Tick(1000);Remote(3,animatedId,1,2);Require(decals.back().started==started);
    // A large declared transfer may run past600s at the unchanged throttle,
    // but silence expires its reservation after120s.
    globals.time+=4;Command(1,{"begin",std::to_string(wire::MaxBytes),"0","1024","1024"});Require(peers[1].upload.size==wire::MaxBytes);
    const auto deadline=peers[1].uploadDeadline;Require(deadline-globals.time>7000);
    for(unsigned i=0;i<8;++i){globals.time+=90;Command(1,{"data",std::to_string(peers[1].upload.id),std::to_string(peers[1].upload.bytes.size()),"AAAA"});Require(peers[1].upload.id!=0);}
    globals.time+=121;Tick();Require(!peers[1].upload.id);
    // Aggregate reservations must not exceed128MiB even before data arrives.
    CSOL_HDSprayActivate();globals.maxClients=32;
    for(unsigned slot=1;slot<=13;++slot)Join(slot,500+slot);Tick(40);
    for(unsigned slot=1;slot<=13;++slot){Command(slot,{"hello",std::to_string(wire::Version)});Command(slot,{"begin",std::to_string(wire::MaxBytes),"0","1024","1024"});Require(bool(peers[slot].upload.id)==(slot<=12));}
    Require(ReservedBytes(nullptr)==12*wire::MaxBytes);CSOL_HDSprayReset();Require(ReservedBytes(nullptr)==0);
    std::cout<<checks<<" matched-SDK HD spray server checks passed; live gameplay pending\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
