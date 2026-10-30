// SPDX-License-Identifier: GPL-3.0-or-later
#include "stock_hands.hpp"
#include "characters.hpp"
#include "stock_hand_sources.hpp"
#include <event_api.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <ostream>

namespace csol::client {
namespace {
struct Entry { const char* native;const char* alias; };
#define CSOL_STOCK_HAND(name) {"models/v_" #name ".mdl","models/csol/hands/v_" #name ".mdl"}
constexpr Entry entries[]{
    CSOL_STOCK_HAND(ak47),CSOL_STOCK_HAND(aug),CSOL_STOCK_HAND(awp),CSOL_STOCK_HAND(c4),
    CSOL_STOCK_HAND(deagle),CSOL_STOCK_HAND(elite),CSOL_STOCK_HAND(famas),CSOL_STOCK_HAND(fiveseven),
    CSOL_STOCK_HAND(flashbang),CSOL_STOCK_HAND(g3sg1),CSOL_STOCK_HAND(galil),CSOL_STOCK_HAND(glock18),
    CSOL_STOCK_HAND(hegrenade),CSOL_STOCK_HAND(knife),CSOL_STOCK_HAND(m249),CSOL_STOCK_HAND(m3),
    CSOL_STOCK_HAND(m4a1),CSOL_STOCK_HAND(mac10),CSOL_STOCK_HAND(mp5),CSOL_STOCK_HAND(p228),
    CSOL_STOCK_HAND(p90),CSOL_STOCK_HAND(scout),CSOL_STOCK_HAND(sg550),CSOL_STOCK_HAND(sg552),
    CSOL_STOCK_HAND(smokegrenade),CSOL_STOCK_HAND(tmp),CSOL_STOCK_HAND(ump45),CSOL_STOCK_HAND(usp),
    CSOL_STOCK_HAND(xm1014),{"models/v_knife.mdl","models/csol/hands/v_knife_w.mdl"}
};
#undef CSOL_STOCK_HAND
struct Cached { model_t* native{};model_t* alias{};int index{};bool checked{}; };
std::array<Cached,std::size(entries)> cache;
const char* lastAlias="";
unsigned draws{};
struct FrameGrid {
    cl_entity_t* entity{};
    model_t* native{};
    model_t* rendered{};
    int count{};
    std::array<float,256> scale{};
} frameGrid;

void UseFrameGrid(cl_entity_t* entity,model_t* native,model_t* rendered)
{
    if(frameGrid.entity==entity && frameGrid.native==native && frameGrid.rendered==rendered)return;
    FrameGrid next{};next.entity=entity;next.native=native;next.rendered=rendered;
    const auto* a=static_cast<const studiohdr_t*>(studio.Mod_Extradata(native));
    const auto* b=static_cast<const studiohdr_t*>(studio.Mod_Extradata(rendered));
    auto valid=[](const studiohdr_t* h){return h && h->numseq>0 && h->numseq<=256 && h->seqindex>=0 &&
        h->length>=h->seqindex && size_t(h->numseq)<=size_t(h->length-h->seqindex)/sizeof(mstudioseqdesc_t);};
    if(valid(a) && valid(b) && a->numseq==b->numseq) {
        next.count=a->numseq;
        const auto* sa=reinterpret_cast<const mstudioseqdesc_t*>(reinterpret_cast<const char*>(a)+a->seqindex);
        const auto* sb=reinterpret_cast<const mstudioseqdesc_t*>(reinterpret_cast<const char*>(b)+b->seqindex);
        for(int i=0;i<next.count;++i)next.scale[i]=sa[i].numframes>1 && sb[i].numframes>1 ?
            float(sb[i].numframes-1)/float(sa[i].numframes-1) : 1.f;
        const int previous=entity->latched.prevsequence;
        // SetupBones owns prevframe and writes it in the rendered frame grid.
        // Keep that value between draws. Only convert a live transition when
        // the same native weapon changes its effective hand model.
        if(frameGrid.entity==entity && frameGrid.native==native && entity->latched.sequencetime!=0 &&
            previous>=0 && previous<frameGrid.count && previous<next.count && frameGrid.scale[previous]>0)
            entity->latched.prevframe*=next.scale[previous]/frameGrid.scale[previous];
    }
    frameGrid=next;
}

bool KnownStockContent(std::string_view name)
{
    const StockHandSource* source=nullptr;
    for(const auto& row:stockHandSources)if(row.name==name){source=&row;break;}
    if(!source)return false;
    try {
        const auto bytes=ReadResource(std::string(name).c_str());
        if(bytes.empty() || bytes.size()>64*1024*1024)return false;
        std::array<unsigned char,32> digest{};
        if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())),ULONG(bytes.size()),digest.data(),ULONG(digest.size()))!=0)return false;
        constexpr char hex[]="0123456789abcdef";
        for(size_t i=0;i<digest.size();++i)if(source->sha256[i*2]!=hex[digest[i]>>4] || source->sha256[i*2+1]!=hex[digest[i]&15])return false;
        return true;
    } catch(const std::exception&) {return false;}
}

bool Compatible(model_t* native,model_t* alias)
{
    const auto* a=static_cast<const studiohdr_t*>(studio.Mod_Extradata(native));
    const auto* b=static_cast<const studiohdr_t*>(studio.Mod_Extradata(alias));
    if(!a || !b || a->version!=10 || b->version!=10 || a->numseq<1 || a->numseq>256 || a->numseq!=b->numseq)return false;
    // Runtime header length includes the engine's repacked texture payload.
    // Only read bounded descriptors/events from its retained prefix.
    auto valid=[](const studiohdr_t* h,int offset,int count,int stride){return offset>=0 && count>=0 &&
        h->length>=offset && size_t(count)<=size_t(h->length-offset)/stride;};
    if(!valid(a,a->seqindex,a->numseq,sizeof(mstudioseqdesc_t)) || !valid(b,b->seqindex,b->numseq,sizeof(mstudioseqdesc_t)))return false;
    const auto* sa=reinterpret_cast<const mstudioseqdesc_t*>(reinterpret_cast<const char*>(a)+a->seqindex);
    const auto* sb=reinterpret_cast<const mstudioseqdesc_t*>(reinterpret_cast<const char*>(b)+b->seqindex);
    for(int i=0;i<a->numseq;++i){
        if(sa[i].numframes<2 || sb[i].numframes<2 || !std::isfinite(sa[i].fps) || !std::isfinite(sb[i].fps) ||
            sa[i].fps<=0 || sb[i].fps<=0)return false;
        const double durationA=double(sa[i].numframes-1)/sa[i].fps,durationB=double(sb[i].numframes-1)/sb[i].fps;
        if(std::memcmp(sa[i].label,sb[i].label,32) || std::abs(durationA-durationB)>1e-6*std::max(durationA,durationB) ||
            sa[i].flags!=sb[i].flags || sa[i].numevents!=sb[i].numevents)return false;
        const auto events=sa[i].numevents;
        if(!valid(a,sa[i].eventindex,events,sizeof(mstudioevent_t)) || !valid(b,sb[i].eventindex,events,sizeof(mstudioevent_t)))return false;
        if(events && std::memcmp(reinterpret_cast<const char*>(a)+sa[i].eventindex,
            reinterpret_cast<const char*>(b)+sb[i].eventindex,events*sizeof(mstudioevent_t)))return false;
    }
    return true;
}
}
void ResetStockHands(){cache={};frameGrid={};lastAlias="";draws=0;}
void NativeHandFrameGrid(cl_entity_t* entity)
{
    if(!entity || entity!=engine.GetViewModel())return;
    if(!entity->model || entity->model->type!=mod_studio){frameGrid={};return;}
    UseFrameGrid(entity,entity->model,entity->model);
}
void StockHandDiagnostics(std::ostream& out)
{
    out<<",\"stockHands\":{\"model\":\""<<lastAlias<<"\",\"draws\":"<<draws<<'}';
}
StockHandScope::StockHandScope(cl_entity_t* ent)
{
    if(!ent || ent!=engine.GetViewModel())return;
    struct TrackNative {
        cl_entity_t* entity;bool aliased{};
        ~TrackNative(){if(!aliased)NativeHandFrameGrid(entity);}
    } track{ent};
    lastAlias="";
    const auto* character=ViewCharacter();
    if(!character || !ent->model || ent->model->type!=mod_studio || !engine.pEventAPI || !engine.pEventAPI->EV_FindModelIndex)return;
    const std::string_view name(ent->model->name,strnlen(ent->model->name,sizeof(ent->model->name)));
    for(size_t i=0;i<std::size(entries);++i){
        if(name!=entries[i].native)continue;
        if(!character->male && name=="models/v_knife.mdl")i=std::size(entries)-1;
        auto& c=cache[i];
        if(!c.checked || c.native!=ent->model){
            c={ent->model,nullptr,0,true};
            // Sequence compatibility alone cannot identify the original gun:
            // custom geometry and skins often retain all native animations.
            // Hash once per model cache; modified guns keep their own model
            // and can take the generic geometry-only hand adapter instead.
            if(!KnownStockContent(name))return;
            // Never load an unadvertised local asset on another server.
            const int index=engine.pEventAPI->EV_FindModelIndex(entries[i].alias);
            auto* alias=index>0?studio.GetModelByIndex(index):nullptr;
            if(alias && std::string_view(alias->name,strnlen(alias->name,sizeof(alias->name)))==entries[i].alias && Compatible(ent->model,alias)){
                c.alias=alias;c.index=index;
            }
        }
        if(c.alias && ent->curstate.sequence>=0){
            const auto* header=static_cast<const studiohdr_t*>(studio.Mod_Extradata(c.alias));
            if(!header || ent->curstate.sequence>=header->numseq)return;
            nativeHeader=static_cast<studiohdr_t*>(studio.Mod_Extradata(ent->model));
            aliasHeader=static_cast<studiohdr_t*>(studio.Mod_Extradata(c.alias));
            entity=ent;model=ent->model;index=ent->curstate.modelindex;
            UseFrameGrid(ent,model,c.alias);
            ent->model=c.alias;ent->curstate.modelindex=c.index;
            track.aliased=true;
            lastAlias=entries[i].alias;++draws;
        }
        return;
    }
}
StockHandScope::~StockHandScope()
{
    if(handedness)handedness->value=savedHandedness;
    if(entity){entity->model=model;entity->curstate.modelindex=index;}
}
void StockHandScope::PrepareCustomRender(int flags)
{
    if(!(flags&STUDIO_RENDER) || !entity || handedness || std::strcmp(model->name,"models/v_knife.mdl") ||
        !engine.pfnGetGameDirectory || !engine.pfnGetCvarPointer)return;
    const char* game=engine.pfnGetGameDirectory();
    if(!game || _stricmp(game,"cstrike"))return;
    auto* value=engine.pfnGetCvarPointer("cl_righthand");
    if(!value || !std::isfinite(value->value))return;
    // The native CS draw toggles knife handedness only for STUDIO_RENDER.
    // Our bone-only call omits that flag. Preserve its temporary value for
    // both bone sampling and culling, then restore the user's setting.
    handedness=value;savedHandedness=value->value;
    value->value=std::trunc(savedHandedness)==0?1.f:0.f;
}
void StockHandScope::ClientEvents()
{
    if(!entity){studio.StudioClientEvents();return;}
    // Event frame numbers belong to native CS, while bone channels retain the
    // original CSO frame grid. Both descriptors cover the same duration.
    auto* alias=entity->model;const int aliasIndex=entity->curstate.modelindex;
    struct Restore {
        cl_entity_t* entity;model_t* model;int index;studiohdr_t* header;
        ~Restore(){entity->model=model;entity->curstate.modelindex=index;studio.StudioSetHeader(header);}
    } restore{entity,alias,aliasIndex,aliasHeader};
    entity->model=model;entity->curstate.modelindex=index;
    studio.StudioSetHeader(nativeHeader);studio.StudioClientEvents();
}
}
