// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "hd_spray.h"
#include "../hd_spray.hpp"
#include <deque>
#include <memory>
#include <set>
#include <charconv>

namespace {
namespace wire=csol::spray;
struct Asset { unsigned id;wire::Bytes bytes;unsigned hash;wire::Dimensions original; };
struct Decal { unsigned id;std::shared_ptr<Asset> asset;int entity,model;Vector position,normal;int serial;float started; };
struct Peer {
    int userid{};bool capable{};std::shared_ptr<Asset> active;
    wire::Transfer upload;double uploadDeadline{},uploadLast{},nextUpload{},nextSend{};
    wire::Dimensions original;
    std::deque<unsigned> pending;std::set<unsigned> known;
    std::shared_ptr<Asset> sending;size_t offset{},acked{};unsigned uploadTokens{};double tokenTime{};
};
std::array<Peer,33> peers;
std::deque<Decal> decals;
unsigned epoch{},nextAsset{},nextDecal{};int message{};
size_t ReservedBytes(const Peer* replacing){
    size_t bytes=0;std::set<const Asset*> assets;
    for(const auto& peer:peers){if(&peer!=replacing)bytes+=peer.upload.size;if(peer.active)assets.insert(peer.active.get());if(peer.sending)assets.insert(peer.sending.get());}
    for(const auto& decal:decals)assets.insert(decal.asset.get());
    for(const auto* asset:assets)bytes+=asset->bytes.size();return bytes;
}
void Send(edict_t* to,const wire::Bytes& b) {
    if(!message||!to||to->free||(to->v.flags&FL_FAKECLIENT))return;
    MESSAGE_BEGIN(MSG_ONE,message,nullptr,to);for(auto c:b)WRITE_BYTE(c);MESSAGE_END();
}
wire::Bytes Packet(wire::MessageType type){wire::Bytes b{type};wire::Put32(b,epoch);return b;}
void Hello(edict_t* e){auto b=Packet(wire::Hello);b.push_back(wire::Version);Send(e,b);}
void Result(edict_t* e,bool ok){auto b=Packet(wire::UploadResult);b.push_back(ok?1:0);Send(e,b);}
unsigned Number(const char* s){unsigned n{};const auto len=std::strlen(s);auto r=std::from_chars(s,s+len,n);wire::Check(len&&r.ec==std::errc{}&&r.ptr==s+len);return n;}
void Queue(Peer& peer,unsigned id){if(peer.pending.size()>=wire::MaxDecals)peer.pending.pop_front();peer.pending.push_back(id);}
void SendPlacement(edict_t* e,const Decal& d){
    auto b=Packet(wire::Placement);wire::Put32(b,d.id);wire::Put32(b,d.asset->id);wire::Put32(b,d.entity);wire::Put32(b,d.model);
    for(int i=0;i<3;++i)wire::PutFloat(b,d.position[i]);for(int i=0;i<3;++i)wire::PutFloat(b,d.normal[i]);
    wire::Put32(b,d.asset->original.width);wire::Put32(b,d.asset->original.height);wire::PutFloat(b,d.started);Send(e,b);
}
}
void CSOL_HDSprayReset(){peers={};decals.clear();if(++epoch==0)++epoch;nextAsset=nextDecal=0;}
void CSOL_HDSprayActivate(){CSOL_HDSprayReset();if(!message)message=REG_USER_MSG(wire::Message,-1);}
bool CSOL_HDSprayCommand(edict_t* e,const char* command){
    if(std::strcmp(command,"csol_hd"))return false;
    int slot=ENTINDEX(e);if(slot<1||slot>32||e->free||(e->v.flags&FL_FAKECLIENT))return true;
    auto& p=peers[slot];const int argc=CMD_ARGC();
    try {
        wire::Check(argc>=3&&Number(CMD_ARGV(2))==epoch&&p.userid==GETPLAYERUSERID(e));
        const char* action=CMD_ARGV(1);
        if(!std::strcmp(action,"hello")){
            wire::Check(argc==4&&Number(CMD_ARGV(3))==wire::Version);
            if(!p.capable){p.capable=true;for(const auto& d:decals)Queue(p,d.id);}return true;
        }
        wire::Check(p.capable);
        if(!std::strcmp(action,"ack")){
            wire::Check(argc==5&&p.sending&&Number(CMD_ARGV(3))==p.sending->id);
            auto offset=Number(CMD_ARGV(4));wire::Check(offset>=p.acked&&offset<=p.offset);p.acked=offset;return true;
        }
        if(!std::strcmp(action,"forget")){wire::Check(argc==4);p.known.erase(Number(CMD_ARGV(3)));return true;}
        if(!std::strcmp(action,"skip")){
            wire::Check(argc==4&&p.sending&&Number(CMD_ARGV(3))==p.sending->id);const auto id=p.sending->id;p.sending.reset();
            std::erase_if(p.pending,[&](unsigned placement){return std::any_of(decals.begin(),decals.end(),[&](const auto& d){return d.id==placement&&d.asset->id==id;});});return true;
        }
        if(!std::strcmp(action,"off")){wire::Check(argc==3);p.active.reset();p.upload={};return true;}
        if(!std::strcmp(action,"begin")){
            wire::Check(argc==7&&gpGlobals->time>=p.nextUpload&&CVAR_GET_FLOAT("sv_allowupload")!=0&&CVAR_GET_FLOAT("sv_send_logos")!=0);
            const wire::Dimensions original{Number(CMD_ARGV(5)),Number(CMD_ARGV(6))};wire::TextureDimensions(original);
            const unsigned length=Number(CMD_ARGV(3));const auto reserved=ReservedBytes(&p);
            wire::Check(length<=wire::MaxBytes&&reserved<=wire::CacheBytes&&length<=wire::CacheBytes-reserved);
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
    }catch(const std::exception&){p.upload={};Result(e,false);}
    return true;
}
void CSOL_HDSprayFrame(){
    if(!message)return;
    // A removed/reused BSP entity must not inherit a former entity's decal.
    for(auto d=decals.begin();d!=decals.end();){auto* e=INDEXENT(d->entity);
        if(d->entity&&(!e||e->free||e->serialnumber!=d->serial||e->v.modelindex!=d->model)){
            auto b=Packet(wire::Remove);wire::Put32(b,d->id);for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)if(peers[i].capable)Send(INDEXENT(i),b);d=decals.erase(d);
        }else ++d;
    }
    for(int slot=1;slot<=std::min(32,gpGlobals->maxClients);++slot){
        auto* e=INDEXENT(slot);auto& p=peers[slot];
        if(!e||e->free||(e->v.flags&FL_FAKECLIENT)||!e->pvPrivateData||!e->v.netname){p={};continue;}
        const int user=GETPLAYERUSERID(e);if(user<=0)continue;
        if(p.userid!=user){p={};p.userid=user;p.nextSend=gpGlobals->time+1;}
        if(p.upload.id&&(gpGlobals->time>=p.uploadDeadline||gpGlobals->time-p.uploadLast>=120)){p.upload={};Result(e,false);}
        if(gpGlobals->time<p.nextSend)continue;p.nextSend=gpGlobals->time+.025;
        if(!p.capable){Hello(e);p.nextSend=gpGlobals->time+2;continue;}
        if(p.sending){
            if(p.offset==p.sending->bytes.size()){
                if(p.acked==p.offset){p.known.insert(p.sending->id);p.sending.reset();}continue;
            }
            if(p.offset-p.acked>=wire::Chunk*8)continue;
            auto b=Packet(wire::Data);wire::Put32(b,p.sending->id);wire::Put32(b,unsigned(p.offset));
            const size_t count=std::min(size_t(wire::Chunk),p.sending->bytes.size()-p.offset);
            b.insert(b.end(),p.sending->bytes.begin()+p.offset,p.sending->bytes.begin()+p.offset+count);Send(e,b);p.offset+=count;
            continue;
        }
        while(!p.pending.empty()){
            const unsigned id=p.pending.front();auto d=std::find_if(decals.begin(),decals.end(),[&](const auto& x){return x.id==id;});
            if(d==decals.end()){p.pending.pop_front();continue;}
            if(!p.known.contains(d->asset->id)){
                p.sending=d->asset;p.offset=p.acked=0;auto b=Packet(wire::Begin);wire::Put32(b,p.sending->id);wire::Put32(b,unsigned(p.sending->bytes.size()));wire::Put32(b,p.sending->hash);Send(e,b);
            }else {SendPlacement(e,*d);p.pending.pop_front();}break;
        }
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
