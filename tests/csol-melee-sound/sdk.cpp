// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production weapon TU, actual ReGameDLL owners and engine ABI.
// The runner grants test-only access to the pending state; no method is replaced.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/weapons/wpn_melee.cpp"
#include <tuple>

namespace {
using namespace sdk_test;
using namespace csol::melee;
struct Sound { std::string sample; int channel{}, pitch{}; };
std::vector<Sound> sounds;
std::vector<std::pair<int,int>> randomCalls;
std::vector<std::string> order;
std::vector<edict_t*> targets;
edict_t* traced{};
int selection{}, damageCalls{}, assertions{}, failed{}, cases{};
bool wall{};
std::function<void()> soundCallback, randomCallback;
void Expect(bool value,const char* label)
{++assertions;if(!value){++failed;std::cerr<<"ASSERT "<<label<<'\n';}}
class SoundTarget final : public CBaseEntity {
public:
    void TraceAttack(entvars_t*,float,Vector,TraceResult*,int) override
    {++damageCalls;order.emplace_back("damage");}
};
void Run(Variant variant,Button button,int animation,Contact contact,int choose,int count=1,int cancel=0)
{
    ++cases;
    auto* engine=Engine::self;
    Require(!g_pGameRules,"isolated SDK owner has no scenario rules");
    engine->Prepare("0");
    auto* ownerEd=engine->Reserve(1,"player");
    auto* player=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ownerEd->v));
    player->pev->health=100;player->pev->deadflag=DEAD_NO;player->pev->modelindex=0;
    auto* weaponEd=engine->Reserve(10,Resource(variant).classname);
    CCSOLMelee* weapon=variant==Variant::DragonSword
        ? static_cast<CCSOLMelee*>(GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<CDragonSword*>(&weaponEd->v)))
        : static_cast<CCSOLMelee*>(GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<CSkullAxe*>(&weaponEd->v)));
    weapon->m_iId=WeaponId(variant);weapon->m_pPlayer=player;player->m_pActiveItem=weapon;
    weapon->prediction.pendingAnimation=animation;weapon->prediction.serial=17;
    originalMode.value=0;Config(variant).damage.base={95,95,91};
    sounds.clear();randomCalls.clear();order.clear();targets.clear();traced=nullptr;
    damageCalls=0;selection=choose;wall=contact==Contact::World;
    soundCallback={};randomCallback={};
    if(contact==Contact::Damageable) for(int i=0;i<count;++i) {
        auto* ed=engine->Reserve(20+i,"sound_target");
        auto* target=GetClassPtr<CCSEntity>(reinterpret_cast<SoundTarget*>(&ed->v));
        target->pev->takedamage=DAMAGE_YES;target->pev->solid=SOLID_BBOX;target->pev->origin=Vector(30,0,0);
        targets.push_back(ed);
    }
    g_engfuncs.pfnMakeVectors=[](const float*){gpGlobals->v_forward=Vector(1,0,0);gpGlobals->v_right=Vector(0,1,0);gpGlobals->v_up=Vector(0,0,1);};
    g_engfuncs.pfnFindEntityInSphere=[](edict_t* start,const float*,float)->edict_t* {
        const int cursor=start?Engine::Index(start):0;traced=nullptr;
        for(auto* ed:targets)if(!ed->free&&Engine::Index(ed)>cursor){traced=ed;return ed;}
        return Engine::At(0);
    };
    g_engfuncs.pfnTraceHull=[](const float*,const float* end,int,int,edict_t*,TraceResult* result){
        *result={};result->flFraction=traced||wall?.5f:1;result->pHit=traced?traced:Engine::At(0);result->vecEndPos=Vector(end[0],end[1],end[2]);};
    engine->onTrace=[](edict_t*,TraceResult& result){result.flFraction=traced?.5f:1;result.pHit=traced;};
    g_engfuncs.pfnCanSkipPlayer=[](const edict_t*){return 0;};
    g_engfuncs.pfnRandomLong=[](int32 low,int32 high)->int32 {
        randomCalls.emplace_back(low,high);const auto callback=randomCallback;if(callback)callback();
        return low==high?low:selection;
    };
    g_engfuncs.pfnEmitSound=[](edict_t*,int channel,const char* sample,float volume,float attenuation,int flags,int pitch){
        Expect(volume==1&&attenuation==ATTN_NORM&&flags==0,"original emit volume/attenuation/flags");
        sounds.push_back({sample,channel,pitch});order.emplace_back("sound");
        const auto callback=soundCallback;if(callback)callback();
    };
    const auto cancelOwner=[&]{player->m_pActiveItem=nullptr;};
    if(cancel==1)soundCallback=cancelOwner;
    if(cancel==2)randomCallback=cancelOwner;
    if(cancel==3)soundCallback=[&]{if(traced&&!traced->free)Engine::Remove(traced);};
    if(cancel==4)soundCallback=[&]{Engine::Remove(ownerEd);};
    if(cancel==5)soundCallback=[&]{player->m_pActiveItem=nullptr;Engine::Remove(weaponEd);};
    engine->events.clear();
    weapon->DelayedAttack();
    if(cancel) {
        Expect(damageCalls==0,"retiring callback precedes damage on SkullAxe");
        Expect(sounds.size()==(cancel==2?0:1),"cancelled attack stops at exact sound boundary");
        if(cancel!=3)Expect(std::none_of(engine->events.begin(),engine->events.end(),[](const auto& row){return row.starts_with("message:");}),
            "cancelled sound boundary cannot publish completion or animation");
    } else {
        const bool dragon=variant==Variant::DragonSword;
        const bool second=button==Button::Primary&&animation==2;
        const char* expected;
        if(dragon) {
            if(contact==Contact::Damageable)expected=second?"weapons/dragonsword_hit2.wav":"weapons/dragonsword_hit1.wav";
            else if(contact==Contact::World)expected="weapons/skullaxe_wall.wav";
            else if(button==Button::Secondary)expected="weapons/dragonsword_stab_hit.wav";
            else expected=second?"weapons/dragonsword_slash2.wav":"weapons/dragonsword_slash1.wav";
        } else {
            constexpr const char* misses[]{"weapons/skullaxe_slash1.wav","weapons/skullaxe_slash2.wav","weapons/skullaxe_miss.wav"};
            expected=contact==Contact::Damageable?"weapons/skullaxe_hit.wav":contact==Contact::World?"weapons/skullaxe_wall.wav":misses[choose];
        }
        const int expectedSounds=!dragon&&contact==Contact::Damageable?count:1;
        Expect(int(sounds.size())==expectedSounds,"original per-target or per-completion sound count");
        for(const auto& sound:sounds) {
            Expect(sound.sample==expected,"original hit/miss table sample selected");
            Expect(sound.channel==(!dragon&&contact==Contact::None?CHAN_AUTO:CHAN_WEAPON),"original result channel");
            Expect(sound.pitch==(!dragon&&contact==Contact::None?94:100),"original result pitch");
        }
        Expect(damageCalls==(contact==Contact::Damageable?count:0),"sound correction preserves target damage dispatch");
        Expect(randomCalls.size()==(dragon?0:sounds.size()),"only original helper paths request random samples");
        for(auto [low,high]:randomCalls)Expect(low==0&&high==(contact==Contact::None?2:0),"original sample-count RNG bounds");
        if(contact==Contact::Damageable) {
            std::vector<std::string> expectedOrder;
            for(int i=0;i<count;++i){if(!dragon)expectedOrder.emplace_back("sound");expectedOrder.emplace_back("damage");}
            if(dragon)expectedOrder.emplace_back("sound");
            Expect(order==expectedOrder,"original sound versus target-damage ordering");
        }
    }
    soundCallback={};randomCallback={};
    if(!ownerEd->free)player->m_pActiveItem=nullptr;
    engine->Clear();
}
}
int main()
{
    try {
        auto engine=std::make_unique<Engine>();
        for(int animation:{1,2,4})for(auto contact:{Contact::None,Contact::World,Contact::Damageable})
            Run(Variant::DragonSword,animation==4?Button::Secondary:Button::Primary,animation,contact,0);
        for(auto button:{Button::Primary,Button::Secondary}) {
            const int animation=button==Button::Primary?8:2;
            for(int random=0;random<3;++random)Run(Variant::SkullAxe,button,animation,Contact::None,random);
            for(auto contact:{Contact::World,Contact::Damageable})Run(Variant::SkullAxe,button,animation,contact,0);
            Run(Variant::SkullAxe,button,animation,Contact::Damageable,0,2);
        }
        Run(Variant::DragonSword,Button::Secondary,4,Contact::Damageable,0,2);
        for(int cancellation:{1,2,3,4,5})Run(Variant::SkullAxe,Button::Secondary,2,Contact::Damageable,0,1,cancellation);
    } catch(const std::exception& error){++failed;std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
    std::cout<<"melee-sound-sdk cases="<<cases<<" assertions="<<assertions<<" failures="<<failed
             <<" SDK checks="<<sdk_test::checks<<" SDK failures="<<sdk_test::failures<<'\n';
    return failed||sdk_test::failures?1:0;
}
