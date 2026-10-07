#include "precompiled.h"
#include "objects.h"
#include <algorithm>
#include <map>

namespace {
// A sprite has no studio hitboxes: ReHLDS SV_SingleClipMoveToEntity selects the
// exact entvars bounding box. renderamt=0 retains AddToFullPack/PM collision;
// EF_NODRAW would incorrectly remove the entity from client prediction.
constexpr char proxy_model[]="sprites/zerogxplode.spr";
constexpr int proxy_marker=0x47434f42;
class MinecraftObject final:public CBaseEntity {
public:
    std::uint64_t key=0;
    unsigned flags=0;
    int ObjectCaps() override {return FCAP_DONT_SAVE|((flags&goldcraft::object_usable)?FCAP_IMPULSE_USE:0);}
    int BloodColor() override {return DONT_BLEED;}
    BOOL TakeDamage(entvars_t* inflictor,entvars_t* attacker,float amount,int bits) override {
        const Vector source=inflictor?inflictor->origin:pev->origin;
        return GoldCraft_ObjectAction(key,1,attacker?ENT(attacker):nullptr,amount,bits,source)?TRUE:FALSE;
    }
    void Use(CBaseEntity* activator,CBaseEntity*,USE_TYPE,float) override {
        if(activator&&activator->IsPlayer())GoldCraft_ObjectAction(key,2,activator->edict(),0,0,activator->pev->origin);
    }
};
struct Entry {MinecraftObject* entity;int serial;};
std::map<std::uint64_t,Entry> objects;
float last_snapshot=0;
bool valid(const Entry& entry){return entry.entity&&!entry.entity->edict()->free&&entry.entity->edict()->serialnumber==entry.serial&&entry.entity->pev->iuser4==proxy_marker;}
}
void GoldCraft_ObjectsPrecache(){PRECACHE_MODEL(const_cast<char*>(proxy_model));}
void GoldCraft_ObjectsReset(bool remove){
    if(remove)for(auto& [key,entry]:objects)if(valid(entry))REMOVE_ENTITY(entry.entity->edict());
    objects.clear();last_snapshot=0;
}
void GoldCraft_ObjectsUpdate(const goldcraft::WorldObjects& snapshot){
    std::set<std::uint64_t> retained;
    for(const auto& o:snapshot.objects)retained.insert(o.key);
    for(auto it=objects.begin();it!=objects.end();)if(!retained.contains(it->first)){
        if(valid(it->second))REMOVE_ENTITY(it->second.entity->edict());it=objects.erase(it);
    }else ++it;
    for(const auto& o:snapshot.objects){
        auto it=objects.find(o.key);MinecraftObject* p=nullptr;
        if(it!=objects.end()&&valid(it->second))p=it->second.entity;
        else {
            // Keep room for native map entities and game-created grenades/drops.
            if(NUMBER_OF_ENTITIES()>=gpGlobals->maxEntities-64)break;
            p=GetClassPtr<CCSEntity>(static_cast<MinecraftObject*>(nullptr));if(!p)break;
            p->pev->classname=MAKE_STRING("goldcraft_object");p->pev->iuser4=proxy_marker;
            p->pev->movetype=MOVETYPE_NONE;p->pev->solid=SOLID_BBOX;p->pev->takedamage=DAMAGE_YES;
            p->pev->rendermode=kRenderTransTexture;p->pev->renderamt=0;p->pev->effects=EF_NOINTERP;
            SET_MODEL(p->edict(),const_cast<char*>(proxy_model));
            objects[o.key]={p,p->edict()->serialnumber};
        }
        p->key=o.key;p->flags=o.flags;p->pev->health=std::max(1.0f,o.health);
        const Vector min(o.min[0],o.min[1],o.min[2]),max(o.max[0],o.max[1],o.max[2]);
        const Vector center=(min+max)*0.5f;
        const Vector relative_min=min-center,relative_max=max-center;
        if(p->pev->mins!=relative_min||p->pev->maxs!=relative_max)SET_SIZE(p->edict(),relative_min,relative_max);
        if(p->pev->origin!=center)SET_ORIGIN(p->edict(),center);
    }
    last_snapshot=gpGlobals->time;
}
void GoldCraft_ObjectsExpire(){if(!objects.empty()&&gpGlobals->time-last_snapshot>1.0f)GoldCraft_ObjectsReset(true);}
std::size_t GoldCraft_ObjectCount(){return objects.size();}
std::vector<std::pair<int,goldcraft::WorldObject>> GoldCraft_ObjectColliders(){
    std::vector<std::pair<int,goldcraft::WorldObject>> result;
    for(const auto& [key,entry]:objects)if(valid(entry)){
        goldcraft::WorldObject o;o.key=key;
        for(int i=0;i<3;++i){o.min[i]=entry.entity->pev->origin[i]+entry.entity->pev->mins[i];o.max[i]=entry.entity->pev->origin[i]+entry.entity->pev->maxs[i];}
        result.emplace_back(entry.entity->entindex(),o);
    }
    return result;
}
std::uint64_t GoldCraft_ObjectKey(edict_t* e){
    if(!e||e->free||!e->pvPrivateData||e->v.iuser4!=proxy_marker||!FClassnameIs(e,"goldcraft_object"))return 0;
    return static_cast<MinecraftObject*>(GET_PRIVATE(e))->key;
}
bool GoldCraft_ObjectBlocksPenetration(edict_t* e){return GoldCraft_ObjectKey(e)&&(static_cast<MinecraftObject*>(GET_PRIVATE(e))->flags&goldcraft::object_block);}
edict_t* GoldCraft_MobObject(std::uint64_t key){
    auto it=objects.find(key);
    return it!=objects.end()&&valid(it->second)&&(it->second.entity->flags&goldcraft::object_mob)?it->second.entity->edict():nullptr;
}
