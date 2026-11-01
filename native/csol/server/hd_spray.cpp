// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "hd_spray.h"
#include "../hd_spray.hpp"
#include "../hd_spray_http.hpp"
#include "../hd_spray_http_server.hpp"
#include <deque>
#include <memory>
#include <set>
#include <charconv>

namespace {
namespace wire=csol::spray;
struct Asset { unsigned id;wire::Bytes bytes;unsigned hash;wire::Dimensions original;wire::http::Body body;wire::http::Token token{}; };
const wire::Bytes& Data(const Asset& asset){return asset.body?asset.body->bytes:asset.bytes;}
struct Decal { unsigned id;std::shared_ptr<Asset> asset;int entity,model;Vector position,normal;int serial;float started; };
struct Peer {
    int userid{};bool capable{},http{};std::shared_ptr<Asset> active;
    std::shared_ptr<wire::http::Upload> httpUpload;unsigned httpId{},httpRequest{};double httpUploadDeadline{},httpDeadline{};
    std::deque<std::weak_ptr<Asset>> prefetch;
    wire::Transfer upload;double uploadDeadline{},uploadLast{},nextUpload{},nextSend{};
    wire::Dimensions original;
    std::deque<unsigned> pending;std::set<unsigned> known;
    std::shared_ptr<Asset> sending;size_t offset{},acked{};unsigned uploadTokens{};double tokenTime{};
};
std::array<Peer,33> peers;
std::deque<Decal> decals;
unsigned epoch{},nextAsset{},nextDecal{};int message{};
wire::http::Server httpServer;std::string publicEndpoint;unsigned advertisedPort{};
cvar_t httpEnabled={"csol_spray_http","1",0,1,nullptr};
void CancelUpload(Peer& p){if(p.httpUpload)p.httpUpload->cancelled=true;p.httpUpload.reset();p.httpId=0;p.upload={};}
void Prefetch(const std::shared_ptr<Asset>& asset){for(auto& peer:peers)if(peer.capable&&peer.http&&!peer.known.contains(asset->id)){
    if(peer.prefetch.size()>=wire::MaxDecals)peer.prefetch.pop_front();peer.prefetch.push_back(asset);}}
size_t ReservedBytes(const Peer* replacing){
    size_t bytes=0;std::set<const Asset*> assets;
    for(const auto& peer:peers){if(&peer!=replacing)bytes+=peer.upload.size;if(peer.active)assets.insert(peer.active.get());if(peer.sending)assets.insert(peer.sending.get());}
    for(const auto& decal:decals)assets.insert(decal.asset.get());
    for(const auto* asset:assets)if(!asset->body)bytes+=asset->bytes.size();return bytes+httpServer.Reserved();
}
void Send(edict_t* to,const wire::Bytes& b) {
    if(!message||!to||to->free||(to->v.flags&FL_FAKECLIENT))return;
    MESSAGE_BEGIN(MSG_ONE,message,nullptr,to);for(auto c:b)WRITE_BYTE(c);MESSAGE_END();
}
wire::Bytes Packet(wire::MessageType type){wire::Bytes b{type};wire::Put32(b,epoch);return b;}
void Hello(edict_t* e){
    // V3 clients retain their bounded reliable transfer path. V4 upgrades to
    // HTTP when available, while still connecting to older V3 servers.
    auto old=Packet(wire::Hello);old.push_back(3);Send(e,old);
    auto b=Packet(wire::Hello);b.push_back(wire::Version);wire::Put32(b,advertisedPort);wire::http::PutString(b,publicEndpoint);Send(e,b);
}
void Result(edict_t* e,bool ok){auto b=Packet(wire::UploadResult);b.push_back(ok?1:0);Send(e,b);}
void HttpResult(edict_t* e,unsigned id,unsigned request,bool ok){auto b=Packet(wire::HttpResult);wire::Put32(b,id);wire::Put32(b,request);b.push_back(ok?1:0);Send(e,b);}
unsigned Number(const char* s){unsigned n{};const auto len=std::strlen(s);auto r=std::from_chars(s,s+len,n);wire::Check(len&&r.ec==std::errc{}&&r.ptr==s+len);return n;}
void Queue(Peer& peer,unsigned id){if(peer.pending.size()>=wire::MaxDecals)peer.pending.pop_front();peer.pending.push_back(id);}
void SendAsset(edict_t* e,Peer& p,const std::shared_ptr<Asset>& asset){
    p.sending=asset;p.offset=p.acked=0;
    if(p.http&&asset->body){auto b=Packet(wire::HttpDownload);wire::Put32(b,asset->id);wire::Put32(b,unsigned(Data(*asset).size()));wire::Put32(b,asset->hash);
        b.insert(b.end(),asset->token.begin(),asset->token.end());Send(e,b);p.httpDeadline=gpGlobals->time+150;}
    else {auto b=Packet(wire::Begin);wire::Put32(b,asset->id);wire::Put32(b,unsigned(Data(*asset).size()));wire::Put32(b,asset->hash);Send(e,b);}
}
void SendPlacement(edict_t* e,const Decal& d){
    auto b=Packet(wire::Placement);wire::Put32(b,d.id);wire::Put32(b,d.asset->id);wire::Put32(b,d.entity);wire::Put32(b,d.model);
    for(int i=0;i<3;++i)wire::PutFloat(b,d.position[i]);for(int i=0;i<3;++i)wire::PutFloat(b,d.normal[i]);
    wire::Put32(b,d.asset->original.width);wire::Put32(b,d.asset->original.height);wire::PutFloat(b,d.started);Send(e,b);
}
}
void CSOL_HDSprayReset(){
    if(g_engfuncs.pfnCVarGetPointer&&CVAR_GET_POINTER("gc_spray_http_backend"))CVAR_SET_FLOAT("gc_spray_http_backend",0);
    httpServer.Stop();peers={};decals.clear();publicEndpoint.clear();advertisedPort=0;if(++epoch==0)++epoch;nextAsset=nextDecal=0;
}
void CSOL_HDSprayActivate(){CSOL_HDSprayReset();if(!message)message=REG_USER_MSG(wire::Message,-1);
    static bool registered=false;if(!registered&&g_engfuncs.pfnCVarRegister){CVAR_REGISTER(&httpEnabled);registered=true;}
    if(CVAR_GET_FLOAT("csol_spray_http")==0)return;
    try{wire::Check(g_engfuncs.pfnCVarGetPointer&&CVAR_GET_POINTER("gc_spray_http_backend")&&CVAR_GET_POINTER("gc_spray_http_front"));
        wire::Check(httpServer.Start("127.0.0.1",0));CVAR_SET_FLOAT("gc_spray_http_backend",httpServer.Port());
    }catch(...){publicEndpoint.clear();advertisedPort=0;ALERT(at_console,"HD spray HTTP unavailable; using bounded game-channel transfers.\n");}
}
bool CSOL_HDSprayCommand(edict_t* e,const char* command){
    if(std::strcmp(command,"csol_hd"))return false;
    int slot=ENTINDEX(e);if(slot<1||slot>32||e->free||(e->v.flags&FL_FAKECLIENT))return true;
    auto& p=peers[slot];const int argc=CMD_ARGC();unsigned request=0;
    try {if(argc<3||Number(CMD_ARGV(2))!=epoch||p.userid!=GETPLAYERUSERID(e))return true;}catch(...){return true;}
    try {
        const char* action=CMD_ARGV(1);
        if(!std::strcmp(action,"hello")){
            wire::Check(argc==4);const auto version=Number(CMD_ARGV(3));wire::Check(version==3||version==wire::Version);
            if(version==wire::Version)p.http=advertisedPort!=0;
            if(!p.capable){p.capable=true;for(const auto& d:decals)Queue(p,d.id);}return true;
        }
        wire::Check(p.capable);
        if(!std::strcmp(action,"ready")){
            wire::Check(argc==4&&p.http&&p.sending&&Number(CMD_ARGV(3))==p.sending->id);p.known.insert(p.sending->id);p.sending.reset();return true;
        }
        if(!std::strcmp(action,"ack")){
            wire::Check(argc==5&&p.sending&&Number(CMD_ARGV(3))==p.sending->id);
            auto offset=Number(CMD_ARGV(4));wire::Check(offset>=p.acked&&offset<=p.offset);p.acked=offset;return true;
        }
        if(!std::strcmp(action,"forget")){wire::Check(argc==4);p.known.erase(Number(CMD_ARGV(3)));return true;}
        if(!std::strcmp(action,"skip")){
            wire::Check(argc==4&&p.sending&&Number(CMD_ARGV(3))==p.sending->id);const auto id=p.sending->id;p.sending.reset();
            std::erase_if(p.pending,[&](unsigned placement){return std::any_of(decals.begin(),decals.end(),[&](const auto& d){return d.id==placement&&d.asset->id==id;});});return true;
        }
        if(!std::strcmp(action,"off")){wire::Check(argc==3);p.active.reset();CancelUpload(p);return true;}
        if(!std::strcmp(action,"begin")){
            if(p.http&&argc==8)request=Number(CMD_ARGV(7));
            wire::Check(argc==(p.http?8:7)&&gpGlobals->time>=p.nextUpload&&CVAR_GET_FLOAT("sv_allowupload")!=0&&CVAR_GET_FLOAT("sv_send_logos")!=0);
            const wire::Dimensions original{Number(CMD_ARGV(5)),Number(CMD_ARGV(6))};wire::TextureDimensions(original);
            const unsigned length=Number(CMD_ARGV(3));CancelUpload(p);const auto reserved=ReservedBytes(&p);
            wire::Check(length<=wire::MaxBytes&&reserved<=wire::CacheBytes&&length<=wire::CacheBytes-reserved);
            if(p.http){wire::Check(request);p.httpRequest=request;p.httpUpload=httpServer.Offer(length,Number(CMD_ARGV(4)),original);p.httpId=++nextAsset;p.nextUpload=gpGlobals->time+3;p.httpUploadDeadline=gpGlobals->time+150;
                auto b=Packet(wire::HttpUpload);wire::Put32(b,p.httpId);wire::Put32(b,request);b.insert(b.end(),p.httpUpload->token.begin(),p.httpUpload->token.end());Send(e,b);return true;}
            p.upload.Begin(++nextAsset,length,Number(CMD_ARGV(4)));
            p.original=original;
            p.uploadLast=gpGlobals->time;p.uploadDeadline=gpGlobals->time+wire::UploadSeconds(length);p.nextUpload=gpGlobals->time+3;p.uploadTokens=wire::UploadChunk*8;p.tokenTime=gpGlobals->time;
            // Return the server-assigned identity. Clients cannot replace another player's image.
            auto b=Packet(wire::UploadResult);b.push_back(2);wire::Put32(b,p.upload.id);Send(e,b);return true;
        }
        if(!std::strcmp(action,"data")){
            wire::Check(argc==6&&p.upload.id&&gpGlobals->time<p.uploadDeadline&&gpGlobals->time-p.uploadLast<120);
            auto bytes=wire::Unbase64(CMD_ARGV(5));
            p.uploadTokens=std::min(3600u,p.uploadTokens+unsigned(std::max(0.0,double(gpGlobals->time)-p.tokenTime)*24000));p.tokenTime=gpGlobals->time;
            wire::Check(bytes.size()<=p.uploadTokens);p.uploadTokens-=unsigned(bytes.size());
            p.uploadLast=gpGlobals->time;
            if(p.upload.Append(Number(CMD_ARGV(3)),Number(CMD_ARGV(4)),bytes)){
                // Both server and receivers independently decode/check dimensions, full
                // pixel count, trailing marker and integrity before accepting the asset.
                wire::CheckDimensions(p.original,wire::DecodeAnimation(p.upload.bytes,false));
                p.active=std::make_shared<Asset>(Asset{p.upload.id,std::move(p.upload.bytes),p.upload.checksum,p.original});p.known.insert(p.active->id);p.upload={};Result(e,true);
            }else if(p.upload.bytes.size()%(wire::UploadChunk*12)==0){
                auto b=Packet(wire::UploadResult);b.push_back(3);wire::Put32(b,p.upload.id);wire::Put32(b,unsigned(p.upload.bytes.size()));Send(e,b);
            }return true;
        }
        wire::Check(false);
    }catch(const std::exception&){if(p.http&&request)HttpResult(e,0,request,false);else if(!p.http)Result(e,false);CancelUpload(p);}
    return true;
}
void CSOL_HDSprayFrame(){
    if(!message)return;
    unsigned front=0;std::string endpoint;
    if(httpServer.Port()&&CVAR_GET_FLOAT("csol_spray_http")!=0){
        try{const auto port=CVAR_GET_FLOAT("gc_spray_http_front");wire::Check(std::isfinite(port)&&port>=0&&port<=65535&&port==std::floor(port));
            front=unsigned(port);endpoint=wire::http::SharedEndpoint(CVAR_GET_STRING("mc_http_public_url"));
        }catch(...){front=0;endpoint.clear();}
    }
    if(front!=advertisedPort||endpoint!=publicEndpoint){
        advertisedPort=front;publicEndpoint=std::move(endpoint);
        for(auto& peer:peers){CancelUpload(peer);peer.http=peer.capable=false;peer.sending.reset();peer.nextSend=0;}
    }
    // A removed/reused BSP entity must not inherit a former entity's decal.
    for(auto d=decals.begin();d!=decals.end();){auto* e=INDEXENT(d->entity);
        if(d->entity&&(!e||e->free||e->serialnumber!=d->serial||e->v.modelindex!=d->model)){
            auto b=Packet(wire::Remove);wire::Put32(b,d->id);for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)if(peers[i].capable)Send(INDEXENT(i),b);d=decals.erase(d);
        }else ++d;
    }
    for(int slot=1;slot<=std::min(32,gpGlobals->maxClients);++slot){
        auto* e=INDEXENT(slot);auto& p=peers[slot];
        if(!e||e->free||(e->v.flags&FL_FAKECLIENT)||!e->pvPrivateData||!e->v.netname){CancelUpload(p);p={};continue;}
        const int user=GETPLAYERUSERID(e);if(user<=0)continue;
        if(p.userid!=user){CancelUpload(p);p={};p.userid=user;p.nextSend=gpGlobals->time+1;}
        if(p.httpUpload){const auto state=p.httpUpload->state.load(std::memory_order_acquire);
            if(state==wire::http::State::Ready&&CVAR_GET_FLOAT("sv_allowupload")!=0&&CVAR_GET_FLOAT("sv_send_logos")!=0){
                try{auto body=p.httpUpload->body;const auto token=httpServer.Publish(body);
                    p.active=std::make_shared<Asset>(Asset{p.httpId,{},p.httpUpload->checksum,p.httpUpload->original,std::move(body),token});
                    p.known.insert(p.httpId);HttpResult(e,p.httpId,p.httpRequest,true);CancelUpload(p);Prefetch(p.active);
                }catch(...){HttpResult(e,p.httpId,p.httpRequest,false);CancelUpload(p);}
            }else if(state==wire::http::State::Failed||gpGlobals->time>=p.httpUploadDeadline||CVAR_GET_FLOAT("sv_allowupload")==0||CVAR_GET_FLOAT("sv_send_logos")==0){HttpResult(e,p.httpId,p.httpRequest,false);CancelUpload(p);}
        }
        if(p.upload.id&&(gpGlobals->time>=p.uploadDeadline||gpGlobals->time-p.uploadLast>=120)){p.upload={};Result(e,false);}
        if(gpGlobals->time<p.nextSend)continue;p.nextSend=gpGlobals->time+.025;
        if(!p.capable){Hello(e);p.nextSend=gpGlobals->time+2;continue;}
        if(p.sending){
            if(p.http&&p.sending->body){if(gpGlobals->time>=p.httpDeadline){const auto id=p.sending->id;p.sending.reset();std::erase_if(p.pending,[&](unsigned placement){return std::any_of(decals.begin(),decals.end(),[&](const auto& d){return d.id==placement&&d.asset->id==id;});});}continue;}
            const auto& bytes=Data(*p.sending);
            if(p.offset==bytes.size()){
                if(p.acked==p.offset){p.known.insert(p.sending->id);p.sending.reset();}continue;
            }
            if(p.offset-p.acked>=wire::Chunk*8)continue;
            auto b=Packet(wire::Data);wire::Put32(b,p.sending->id);wire::Put32(b,unsigned(p.offset));
            const size_t count=std::min(size_t(wire::Chunk),bytes.size()-p.offset);
            b.insert(b.end(),bytes.begin()+p.offset,bytes.begin()+p.offset+count);Send(e,b);p.offset+=count;
            continue;
        }
        while(!p.pending.empty()){
            const unsigned id=p.pending.front();auto d=std::find_if(decals.begin(),decals.end(),[&](const auto& x){return x.id==id;});
            if(d==decals.end()){p.pending.pop_front();continue;}
            if(!p.known.contains(d->asset->id)){
                SendAsset(e,p,d->asset);
            }else {SendPlacement(e,*d);p.pending.pop_front();}break;
        }
        while(!p.sending&&!p.prefetch.empty()){auto asset=p.prefetch.front().lock();p.prefetch.pop_front();if(asset&&!p.known.contains(asset->id))SendAsset(e,p,asset);}
        // Only IDs are retained here; no unbounded image/history cache.
        if(p.known.size()>128){p.known.clear();}
    }
}
bool CSOL_HDSprayTrace(edict_t* owner,const TraceResult& tr){
    if(!owner)return false;int slot=ENTINDEX(owner);if(slot<1||slot>32)return false;
    auto& p=peers[slot];if(!p.capable||!p.active||p.userid!=GETPLAYERUSERID(owner)||CVAR_GET_FLOAT("sv_send_logos")==0)return false;
    if(tr.flFraction==1||!tr.pHit)return true;
    // Trace and cooldown are the original CSprayCan/impulse201 server path.
    // Images never supply coordinates, entity indices, normals or an owner.
    int entity=ENTINDEX(tr.pHit);if(entity&&tr.pHit->v.solid!=SOLID_BSP)return true;
    Decal d{++nextDecal,p.active,entity,tr.pHit->v.modelindex,tr.vecEndPos,tr.vecPlaneNormal,tr.pHit->serialnumber,gpGlobals->time};
    if(entity){
        const auto delta=d.position-tr.pHit->v.origin;Vector f,r,u;UTIL_MakeVectorsPrivate(tr.pHit->v.angles,f,r,u);
        d.position=Vector(DotProduct(delta,f),-DotProduct(delta,r),DotProduct(delta,u));
        const auto n=d.normal;d.normal=Vector(DotProduct(n,f),-DotProduct(n,r),DotProduct(n,u));
    }
    if(decals.size()==wire::MaxDecals){auto b=Packet(wire::Remove);wire::Put32(b,decals.front().id);
        for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)if(peers[i].capable)Send(INDEXENT(i),b);decals.pop_front();}
    decals.push_back(std::move(d));for(auto& peer:peers)if(peer.capable)Queue(peer,nextDecal);return true;
}
