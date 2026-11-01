// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the real prediction dispatcher, inventory message handlers and K3
// event callback. Only the engine/stock-client boundary is controlled here.
#define main ExistingWeaponChecks
#include "../csol-weapon-slots/client.cpp"
#undef main
#include "../../native/csol/client/k3.hpp"
#include "../../native/csol/k3.hpp"
#include <pm_defs.h>
#include <cmath>
#include <tuple>

namespace csol::client {
// Other UI owners are outside the weapon-switch contract. Keep the current
// production session TU linked while disabling unrelated skill/spray UI.
void ResetSpray(){}
void ResetCharacterSkillEffects(){}
bool CharacterSkillInfiniteAmmoFor(int){return false;}
bool DrawCharacterSkillInfiniteAmmo(int,int,int,int,int,int){return false;}
bool TryActivateScenarioAbility(){return false;}
bool TryScenarioHealingSlot(int){return false;}
}

namespace {
int ownershipChecks{}, ownershipFailures{}, eventSounds{}, eventTraces{}, eventShells{}, predictionPushes{}, predictionPops{};
int localHeightReads{}, worldSoundOwner{}, stockCalls{}, stockId{}, stockRun{}, stockSelection{}, stockDeploys{};
bool stockAnimationCache{}, missingViewmodel{}, roundTripDuringStock{};
int cachedStockAnimation{}, cachedStockWeapon{}, engineAnimation{}, engineBody{}, stockCacheWrites{}, currentDuringStock{};
float localWeaponPrediction=1;
decltype(engine.pfnGetCvarFloat) ordinaryCvarFloat{};
std::string ownershipCase, eventPath;
void (*k3Event)(event_args_t*){};
event_api_t ownershipEvents{};
efx_api_t ownershipEffects{};
physent_t ownershipPhysical{};
std::array<float,3> shellOrigin{}, traceOrigin{};

void Expect(bool value,const char* reason)
{
    ++ownershipChecks;
    if(!value){++ownershipFailures;std::cerr<<"FAIL "<<ownershipCase<<": "<<reason<<'\n';}
}
bool Near(float first,float second){return std::abs(first-second)<.0001f;}
struct WeaponCase {int id,capacity,draw;};
constexpr WeaponCase weaponCases[]{{220,1,4},{144,20,2},{725,80,BeamDraw},{726,80,BeamDraw},{197,150,4}};
const WeaponCase* FindCase(int id)
{
    for(const auto& weapon:weaponCases)if(weapon.id==id)return &weapon;
    return nullptr;
}
void Definitions()
{
    for(const auto& definition:std::vector<WeaponDefinition>{
        {220,1,6,14,-1,29,-1,1,24,"weapon_tknifeex2"},
        {144,0,1,17,-1,20,-1,20,0,"weapon_cannon"},
        {725,0,8,15,16,200,100,80,0,"weapon_beamgun"},
        {726,0,9,15,16,200,100,80,0,"weapon_beamgunle"},
        {197,0,1,11,-1,200,-1,150,0,"weapon_k3"}})
        Send(WeaponDefinitionMessage,EncodeWeaponDefinition(definition));
    Legacy();
    for(const auto& [name,id,slot]:{std::tuple{"weapon_knife",29,2},std::tuple{"weapon_usp",16,1}})
    {
        std::vector<std::byte> bytes;
        for(const char* c=name;;++c){bytes.push_back(std::byte(*c));if(!*c)break;}
        for(int value:{255,255,255,255,slot,0,id,0})bytes.push_back(std::byte(value));
        Send("WeaponList",bytes);
    }
    WeaponOwnership owned{};owned[28]=true;
    for(const auto& weapon:weaponCases)owned[weapon.id]=true;
    Owned(owned);
}
void SelectCurrent(int id,int state=1)
{
    const auto* weapon=FindCase(id);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({id,state,weapon?weapon->capacity:id?30:-1,id?200:0,0}));
    Expect(CurrentExtendedWeapon().id==id&&CurrentExtendedWeapon().state==state,"real Current handler accepted selection");
}
void ClearObservations()
{
    weaponAnimations.clear();eventSounds=eventTraces=eventShells=0;
    predictionPushes=predictionPops=localHeightReads=stockCalls=stockDeploys=0;stockId=stockRun=stockSelection=-1;
    weaponView.curstate.sequence=63;weaponView.curstate.body=7;weaponView.curstate.effects=0;
    K3CrosshairShot();CannonCrosshairShot();
}
void Begin(const std::string& name,bool definitions=true)
{
    ownershipCase=name;ResetWeapons(true);ResetCannonEffects();
    stockAnimationCache=false;missingViewmodel=roundTripDuringStock=false;localWeaponPrediction=1;
    cachedStockAnimation=cachedStockWeapon=engineAnimation=engineBody=stockCacheWrites=currentDuringStock=0;
    if(definitions)Definitions();
    ClearObservations();
}
local_state_t Snapshot(int id)
{
    local_state_t state{};const auto* weapon=FindCase(id);
    state.client.m_iId=id;state.client.health=100;state.client.iuser3=1;
    state.client.flags=FL_ONGROUND;state.client.viewmodel=12;
    state.client.weaponanim=weapon?weapon->draw:4;state.client.vuser4[1]=200;
    auto& data=state.weapondata[ActiveWeaponSlot];data.m_iId=id;data.m_iClip=weapon?weapon->capacity:30;
    data.m_flNextPrimaryAttack=data.m_flNextSecondaryAttack=-.001f;data.m_flTimeWeaponIdle=20;
    if(id==197)data.fuser1=.2f;
    return state;
}
local_state_t Dispatch(local_state_t source,int run=1,int buttons=IN_ATTACK,int selection=0)
{
    local_state_t result{};usercmd_t command{};command.buttons=short(buttons);command.msec=10;command.weaponselect=byte(selection);
    const auto originalCommand=command;
    const int id=source.client.m_iId,calls=stockCalls;
    WeaponPostRunCmd(&source,&result,&command,run,now,123);
    Expect(stockCalls==calls+1,"dispatcher invokes actual stock boundary once");
    Expect(stockId==(id>=LegacyWeaponLimit?0:id),"extended ID masked at stock ABI boundary");
    Expect(source.client.m_iId==id,"source historical identity preserved");
    Expect(std::memcmp(&command,&originalCommand,sizeof(command))==0,"caller command is never left modified");
    if(!selection||id>=LegacyWeaponLimit)
    {
        Expect(result.client.m_iId==id,"historical identity preserved without stock selection");
        Expect(result.client.viewmodel==source.client.viewmodel&&result.client.iuser3==source.client.iuser3,"historical model and permission preserved");
    }
    return result;
}
void NoLocalPresentation()
{
    Expect(weaponAnimations.empty(),"stale/replay command emits no animation");
    Expect(eventSounds==0&&eventTraces==0&&eventShells==0,"stale/replay command emits no predicted world event");
    Expect(weaponView.curstate.sequence==63&&weaponView.curstate.body==7&&weaponView.curstate.effects==0,
        "new viewmodel remains untouched");
    Expect(!K3CrosshairShot()&&!CannonCrosshairShot(),"no stale crosshair recoil");
}
void ExtendedMatrix()
{
    for(const auto& weapon:weaponCases)
    {
        Begin(std::to_string(weapon.id)+"/silent-oracle");SelectCurrent(weapon.id);
        const auto reference=Dispatch(Snapshot(weapon.id),0);
        Expect(reference.weapondata[0].m_iClip==weapon.capacity-1,"silent historical command still consumes one shot");
        Expect(reference.client.weaponanim!=weapon.draw&&reference.weapondata[0].m_flNextPrimaryAttack>0,
            "silent historical attack computes animation and cooldown");
        const int another=weapon.id==144?197:144;
        struct Target {int id,state;bool allowed;const char* label;};
        const Target targets[]{
            {28,1,false,"stock"},{another,1,false,"other-extended"},{0,0,false,"no-weapon"},
            {weapon.id,0,false,"same-inactive"},{another,0,false,"other-inactive"},{weapon.id,1,true,"same-active"}};
        for(const auto& target:targets)for(int run:{0,1})
        {
            Begin(std::to_string(weapon.id)+"/"+target.label+"/run"+std::to_string(run));
            SelectCurrent(target.id,target.state);ClearObservations();
            const auto result=Dispatch(Snapshot(weapon.id),run);
            Expect(stockRun==(target.allowed?run:0),"only selected extended weapon may emit presentation");
            Expect(std::memcmp(&result.weapondata[0],&reference.weapondata[0],sizeof(weapon_data_t))==0&&
                result.client.weaponanim==reference.client.weaponanim&&Near(result.client.m_flNextAttack,reference.client.m_flNextAttack)&&
                result.client.vuser4[1]==reference.client.vuser4[1],"ownership gate preserves complete historical weapon computation");
            if(!target.allowed||!run)
            {
                NoLocalPresentation();
                Expect(K3PredictedAmmo()==-1&&CannonPredictedAmmo()==-1,"silent dispatch does not install predicted HUD ammo");
            }
            else
            {
                Expect(weaponAnimations.size()==1,"selected real shot requests animation once");
                Expect(weaponView.curstate.sequence==63,"animation request is separate from render commit");
                if(weapon.id==197)Expect(K3PredictedAmmo()==149&&eventSounds==1&&eventTraces==1&&K3CrosshairShot(),"K3 normal effects retained");
                if(weapon.id==144)Expect(CannonPredictedAmmo()==19&&eventSounds==1&&CannonCrosshairShot(),"cannon normal effects retained");
            }
        }
    }
}
void StockBoundary()
{
    for(int id=1;id<LegacyWeaponLimit;++id)for(int run:{0,1})
    {
        Begin("stock/ordinary-server/"+std::to_string(id),false);
        const auto ordinary=Dispatch(Snapshot(id),run);
        Expect(stockRun==run&&ordinary.weapondata[0].m_iClip==29,"ordinary server retains stock prediction and runFunctions");
        Expect(weaponAnimations.size()==std::size_t(run),"ordinary stock presentation preserved");
        Begin("stock/extended-current/"+std::to_string(id));SelectCurrent(197);ClearObservations();
        const auto stale=Dispatch(Snapshot(id),run);
        Expect(stockRun==0&&stale.weapondata[0].m_iClip==29,"stock history computes silently after selecting extended weapon");
        NoLocalPresentation();
        Begin("stock/stock-selection/"+std::to_string(id));SelectCurrent(28);ClearObservations();
        Dispatch(Snapshot(id),run);
        Expect(stockRun==run&&weaponAnimations.size()==std::size_t(run),"stock-to-stock command prediction is not delayed by Current");
    }
}
void StockSelectionBoundary()
{
    // The real B client selection evidence is independent of this controlled
    // boundary. Selection computes state in silent replay; its Deploy uses
    // force=0 and the final forced reconciliation remains runFunctions-gated.
    for(int run:{0,1})for(int buttons:{0,IN_ATTACK})
    {
        Begin("stock-select/ordinary/run"+std::to_string(run),false);
        auto result=Dispatch(Snapshot(28),run,buttons,16);
        Expect(stockRun==run&&stockSelection==16&&stockDeploys==1,"ordinary native selection retains supplied command and runFunctions");
        Expect(result.client.m_iId==16&&result.client.viewmodel==41&&result.client.weaponanim==136,
            "ordinary native selection computes historical deploy state on either pass");
        Expect(weaponAnimations.size()==std::size_t(run*(1+(buttons!=0))),"silent native selection does not emit an animation");
        Begin("stock-select/stock-current/run"+std::to_string(run));SelectCurrent(28);ClearObservations();
        result=Dispatch(Snapshot(28),run,buttons,16);
        Expect(stockRun==run&&stockDeploys==1&&result.client.m_iId==16,"stock-to-stock same-command selection remains native");
        Expect(weaponAnimations.size()==std::size_t(run*(1+(buttons!=0))),"stock-to-stock real pass retains deploy presentation");
        for(const auto& current:weaponCases)
        {
            Begin("stock-select/extended-current"+std::to_string(current.id)+"/run"+std::to_string(run));
            SelectCurrent(current.id);ClearObservations();result=Dispatch(Snapshot(28),run,buttons,16);
            NoLocalPresentation();
            Expect(stockRun==0&&stockSelection==16&&stockDeploys==1&&result.client.m_iId==16&&result.client.viewmodel==41,
                "cross-boundary old selection computes original native state without presentation");
        }
        for(bool forget:{false,true})
        {
            Begin(forget?"stock-select/reset-forget":"stock-select/reset-keep");SelectCurrent(197);
            ResetWeapons(forget);ClearObservations();result=Dispatch(Snapshot(28),run,buttons,16);
            Expect(stockRun==run&&stockDeploys==1&&result.client.m_iId==16,"Reset restores native selection before next valid Current");
        }
    }
    for(const auto& old:weaponCases)
    {
        Begin("stock-select/old-extended"+std::to_string(old.id));SelectCurrent(28);ClearObservations();
        Dispatch(Snapshot(old.id),1,0,16);NoLocalPresentation();
        Expect(stockDeploys==0,"stock ID0 early return preserves old extended identity despite nonzero selection");
    }
}
auto HudSprites()
{
    spriteDraws.clear();spriteColors.clear();rectangles.clear();WeaponRedraw(now,0);
    return spriteDraws;
}
void CurrentCacheSurvives()
{
    for(const auto& current:weaponCases)for(int oldId:{28,220,144,725,726,197})
    {
        if(oldId==current.id)continue;
        Begin("current-cache/"+std::to_string(current.id)+"/old"+std::to_string(oldId));SelectCurrent(current.id);
        Dispatch(Snapshot(current.id));
        const auto expectedHud=HudSprites();
        const auto expectedK3=K3PredictedAmmo(),expectedCannon=CannonPredictedAmmo();
        Expect(!expectedHud.empty(),"active custom weapon draws actual ammunition HUD");
        ClearObservations();Dispatch(Snapshot(oldId));NoLocalPresentation();
        Expect(K3PredictedAmmo()==expectedK3&&CannonPredictedAmmo()==expectedCannon,"late history does not clear another weapon prediction cache");
        Expect(HudSprites()==expectedHud,"late history does not change current HUD ammunition");
    }
}
void SessionAndBadPackets()
{
    for(bool forget:{false,true})
    {
        Begin(forget?"reset/forget":"reset/keep");SelectCurrent(197);
        ResetWeapons(forget);ClearObservations();Dispatch(Snapshot(28));
        Expect(stockRun==1&&weaponAnimations.size()==1,"Reset restores ordinary stock presentation before next valid Current");
        if(forget)Definitions();
        ClearObservations();Dispatch(Snapshot(144));
        Expect(stockRun==1&&weaponAnimations.size()==1,"retained/new definitions alone do not establish current-state ownership");
        SelectCurrent(197);ClearObservations();Dispatch(Snapshot(28));
        Expect(stockRun==0&&weaponAnimations.empty(),"next valid Current re-establishes cross-boundary ownership");
    }
    Begin("valid-current-before-definitions",false);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({197,1,150,200,0}));Definitions();ClearObservations();Dispatch(Snapshot(28));
    Expect(stockRun==1,"pre-definition Current is rejected without arming gate");
    std::vector<std::vector<std::byte>> bad{{},{std::byte{1}},{std::byte{1},std::byte{197}}};
    auto packet=EncodeWeaponCurrent({197,1,150,200,0});packet[0]=std::byte{255};bad.push_back(packet);
    packet=EncodeWeaponCurrent({197,1,150,200,0});packet.pop_back();bad.push_back(packet);
    packet=EncodeWeaponCurrent({197,1,150,200,0});packet.push_back(std::byte{});bad.push_back(packet);
    bad.push_back(EncodeWeaponCurrent({999,1,1,0,0}));
    for(std::size_t index=0;index<bad.size();++index)
    {
        Begin("bad-current/"+std::to_string(index));Send(WeaponCurrentMessage,bad[index]);ClearObservations();
        Dispatch(Snapshot(28));Expect(stockRun==1,"invalid Current cannot establish ownership gate");
        SelectCurrent(197);Send(WeaponCurrentMessage,bad[index]);ClearObservations();Dispatch(Snapshot(28));
        Expect(stockRun==0&&CurrentExtendedWeapon().id==197,"invalid Current cannot release existing ownership gate");
    }
    Begin("null-current");messages[WeaponCurrentMessage](WeaponCurrentMessage,10,nullptr);Dispatch(Snapshot(28));
    Expect(stockRun==1,"null payload cannot establish ownership gate");
}
void LateK3Events()
{
    struct Target {int id,state;bool localAllowed;const char* label;};
    const Target targets[]{{28,1,false,"stock"},{144,1,false,"cannon"},{725,1,false,"beam"},
        {726,1,false,"beam-le"},{220,1,false,"knife"},{0,0,false,"none"},{197,0,false,"inactive"},{197,1,true,"active"}};
    for(const auto& target:targets)for(bool remote:{false,true})for(bool duck:{false,true})
    {
        Begin(std::string("k3-event/")+target.label+(remote?"/remote":"/local")+(duck?"/duck":"/stand"));
        SelectCurrent(target.id,target.state);ClearObservations();
        event_args_t args{};args.entindex=remote?2:1;args.ducking=duck;args.origin[0]=100;args.origin[1]=200;args.origin[2]=300;
        k3Event(&args);
        Expect(eventSounds==1&&eventTraces==1&&eventShells==1&&worldSoundOwner==args.entindex,"late or remote world sound, shell and bullet are retained");
        Expect(predictionPushes==1&&predictionPops==1,"event trace balances prediction state");
        const bool localView=!remote&&target.localAllowed;
        Expect(weaponAnimations.size()==std::size_t(localView)&&K3CrosshairShot()==localView,"local animation/recoil belongs only to selected K3");
        Expect(bool(weaponView.curstate.effects&EF_MUZZLEFLASH)==localView&&weaponView.curstate.body==7,
            "late event cannot flash or replace new hands");
        Expect(weaponView.curstate.sequence==63,"event animation request does not synchronously render");
        if(localView)
        {
            Expect(shellOrigin==std::array<float,3>{901,902,903},"selected K3 shell uses viewmodel attachment");
            Expect(Near(traceOrigin[2],377)&&localHeightReads==2,"selected K3 shell and bullet use local viewheight");
        }
        else
        {
            Expect(shellOrigin==std::array<float,3>{120,196,duck?300.f:316.f},"late/remote shell uses historical event origin and posture");
            Expect(Near(traceOrigin[2],duck?312.f:328.f)&&localHeightReads==0,"late/remote bullet never reads new gun local viewheight");
        }
    }
}
// Matched B client.dll: HUD_SendWeaponAnim writes its private (animation,
// weapon) cache before calling the engine. Its final reconcile compares that
// cache, not the engine's animation global or the later render entity state.
void StockAnimation(int sequence,int weapon,int body,int run,bool force=false)
{
    if(!run&&!force)return;
    if(stockAnimationCache){cachedStockAnimation=sequence;cachedStockWeapon=weapon;++stockCacheWrites;}
    engine.pfnWeaponAnim(sequence,body);
}
local_state_t KnifeSnapshot(int sequence=3)
{
    auto state=Snapshot(29);state.client.weaponanim=sequence;return state;
}
void ArmStockReturn(const std::string& name,int custom=197,int cachedSequence=3,int buttons=0)
{
    Begin(name);stockAnimationCache=true;cachedStockAnimation=cachedSequence;cachedStockWeapon=29;
    engineAnimation=cachedSequence;SelectCurrent(custom);
    Dispatch(Snapshot(custom),1,buttons);
    Expect(cachedStockAnimation==cachedSequence&&cachedStockWeapon==29&&stockCacheWrites==0,
        "custom engine animation never changes stock getter cache");
    SelectCurrent(29);ClearObservations();
    Expect(PendingWeaponAnimationSync(29)!=0,"extended-to-stock Current arms one pending synchronization");
}
void StockCacheHandoff()
{
    for(const auto& custom:weaponCases)for(int sequence:{0,3,5})
    {
        ArmStockReturn("stock-cache/custom"+std::to_string(custom.id)+"/anim"+std::to_string(sequence),custom.id,sequence,IN_ATTACK);
        const int previousEngine=engineAnimation;
        Expect(previousEngine!=sequence,"fixture separates engine animation from cached stock animation");
        const auto source=KnifeSnapshot(sequence);
        const auto replay=Dispatch(source,0,0);
        Expect(engineAnimation==previousEngine&&stockCacheWrites==0&&weaponAnimations.empty(),
            "silent replay preserves both independent animation stores");
        Expect(PendingWeaponAnimationSync(29)!=0,"silent replay does not consume handoff");
        const auto result=Dispatch(source,1,0);
        Expect(engineAnimation==sequence&&weaponAnimations.size()==1&&engineBody==7,
            "first live stock snapshot restores actual engine sequence and current body once");
        Expect(stockCacheWrites==0&&cachedStockAnimation==sequence&&cachedStockWeapon==29,
            "cached equality really suppresses stock engine output; adapter repairs separate engine state");
        Expect(std::memcmp(&replay,&result,sizeof(result))==0,"presentation synchronization does not change predicted state");
        Expect(!PendingWeaponAnimationSync(29),"live correction consumes handoff");
        SelectCurrent(29); // Ordinary clip/reserve Current packets must not re-arm.
        for(int step=0;step<4;++step)Dispatch(result,1,0);
        Expect(weaponAnimations.size()==1&&weaponView.curstate.sequence==63,
            "later frames never restart animation even while render entity is stale");
    }
    ArmStockReturn("stock-cache/K3-draw-retained",197,3);
    Expect(engineAnimation==4,"real K3 predictor puts Draw=4 in engine while knife getter remains Draw=3");
    Dispatch(KnifeSnapshot(),1,0);
    Expect(engineAnimation==3&&cachedStockAnimation==3&&stockCacheWrites==0,
        "K3 Draw cannot remain as knife StabHit after first matching live snapshot");

    ArmStockReturn("stock-cache/native-reconcile",197,0);
    Dispatch(KnifeSnapshot(3),1,0);
    Expect(engineAnimation==3&&cachedStockAnimation==3&&cachedStockWeapon==29&&stockCacheWrites==1,
        "ordinary stock mismatch still updates its own getter cache");
    Expect(weaponAnimations.size()==2,"stock repair and one handoff repair occur within one command");
    Dispatch(KnifeSnapshot(3),1,0);
    Expect(weaponAnimations.size()==2,"same-command correction does not become a per-frame restart");

    ArmStockReturn("stock-cache/native-id-mismatch");cachedStockWeapon=28;
    Dispatch(KnifeSnapshot(3),1,0);
    Expect(engineAnimation==3&&cachedStockWeapon==29&&stockCacheWrites==1,
        "stock ID getter mismatch independently triggers original reconciliation");

    ArmStockReturn("stock-cache/legitimate-secondary",197,3);
    auto attacked=Dispatch(KnifeSnapshot(3),1,IN_ATTACK2);
    Expect(attacked.client.weaponanim==5&&engineAnimation==5&&cachedStockAnimation==5,
        "real first secondary attack is retained rather than replaced with guessed Draw");
    const auto count=weaponAnimations.size();Dispatch(attacked,1,0);
    Expect(weaponAnimations.size()==count,"secondary animation continues on the following command");
    Dispatch(attacked,1,IN_ATTACK);
    Expect(engineAnimation==6,"later legitimate primary knife attack remains available");
}
void StockCacheLifecycle()
{
    ArmStockReturn("stock-cache/old-custom-history");
    const int before=engineAnimation;Dispatch(Snapshot(197),1,0);
    Expect(engineAnimation==before&&weaponAnimations.empty()&&PendingWeaponAnimationSync(29),
        "old custom snapshot cannot consume or replay the new stock handoff");
    Dispatch(KnifeSnapshot(),1,0);Expect(engineAnimation==3,"following matching stock snapshot can still correct");

    ArmStockReturn("stock-cache/invalid-packet");
    auto bad=EncodeWeaponCurrent({29,1,30,200,0});bad[0]=std::byte{255};Send(WeaponCurrentMessage,bad);
    Dispatch(KnifeSnapshot(),1,0);Expect(engineAnimation==3,"malformed Current does not cancel pending correction");

    for(int invalid:{0,1,2})
    {
        ArmStockReturn("stock-cache/ineligible"+std::to_string(invalid));auto source=KnifeSnapshot();
        if(invalid==0)source.client.health=0;
        if(invalid==1)source.client.deadflag=1;
        if(invalid==2)localWeaponPrediction=0;
        const int saved=engineAnimation;Dispatch(source,1,0);
        Expect(weaponAnimations.empty()&&engineAnimation==saved&&!PendingWeaponAnimationSync(29),
            "death and cl_lw0 cancel handoff without local animation");
        localWeaponPrediction=1;Dispatch(KnifeSnapshot(),1,0);
        Expect(weaponAnimations.empty(),"cancelled handoff cannot replay after a later alive/predicted snapshot");
    }
    for(bool unavailable:{false,true})
    {
        ArmStockReturn(unavailable?"stock-cache/null-view-entity":"stock-cache/zero-view-index");
        auto source=KnifeSnapshot();if(unavailable)missingViewmodel=true;else source.client.viewmodel=0;
        Dispatch(source,1,0);
        Expect(weaponAnimations.empty()&&PendingWeaponAnimationSync(29),"unavailable model defers synchronization");
        missingViewmodel=false;Dispatch(KnifeSnapshot(),1,0);
        Expect(engineAnimation==3&&weaponAnimations.size()==1,"available model receives deferred correction once");
    }
    for(bool forget:{false,true})
    {
        ArmStockReturn(forget?"stock-cache/reset-forget":"stock-cache/reset-keep");
        ResetWeapons(forget);Dispatch(KnifeSnapshot(),1,0);
        Expect(weaponAnimations.empty()&&!PendingWeaponAnimationSync(29),"session reset cancels pending presentation");
    }
    for(int replacement:{0,16,197})
    {
        ArmStockReturn("stock-cache/new-current"+std::to_string(replacement));
        SelectCurrent(replacement,replacement?1:0);Dispatch(KnifeSnapshot(),1,0);
        Expect(weaponAnimations.empty()&&!PendingWeaponAnimationSync(29),"new Current revokes old weapon token");
    }
    ArmStockReturn("stock-cache/current-during-downstream");currentDuringStock=197;
    Dispatch(KnifeSnapshot(),1,0);
    Expect(weaponAnimations.empty()&&!PendingWeaponAnimationSync(29),
        "Current change inside stock callback prevents stale post-callback correction");

    ArmStockReturn("stock-cache/new-generation-during-downstream");roundTripDuringStock=true;
    Dispatch(KnifeSnapshot(),1,0);
    Expect(weaponAnimations.empty()&&PendingWeaponAnimationSync(29),
        "old token cannot consume a newer same-ID transition created inside stock callback");
    Dispatch(KnifeSnapshot(),1,0);
    Expect(engineAnimation==3&&weaponAnimations.size()==1,"next command synchronizes the newer generation");

    ArmStockReturn("stock-cache/stock-select-in-same-command");
    auto selected=Dispatch(KnifeSnapshot(),1,0,16);
    Expect(selected.client.m_iId==16&&engineAnimation==136&&!PendingWeaponAnimationSync(29),
        "same-command native Deploy is not overwritten by obsolete source-weapon correction");
    Expect(weaponAnimations.size()==2&&stockCacheWrites==2,
        "only native Deploy plus native reconcile emit at same-command selection boundary");

    Begin("stock-cache/ordinary-server",false);stockAnimationCache=true;
    cachedStockAnimation=3;cachedStockWeapon=29;engineAnimation=4;
    Dispatch(KnifeSnapshot(),1,0);
    Expect(engineAnimation==4&&weaponAnimations.empty(),"server without extended Current retains untouched stock behavior");
}
void EngineBoundary()
{
    players[1].index=1;engine.GetLocalPlayer=[](){return &players[1];};engine.GetViewModel=[]()->cl_entity_t*{return missingViewmodel?nullptr:&weaponView;};
    weaponView.attachment[1][0]=901;weaponView.attachment[1][1]=902;weaponView.attachment[1][2]=903;
    engine.pfnWeaponAnim=[](int sequence,int body){weaponAnimations.push_back({sequence,body});engineAnimation=sequence;engineBody=body;};
    ordinaryCvarFloat=engine.pfnGetCvarFloat;
    engine.pfnGetCvarFloat=[](const char* name)->float{
        return !std::strcmp(name,"cl_lw")?localWeaponPrediction:ordinaryCvarFloat(name);};
    engine.pfnAngleVectors=[](const float*,float* forward,float* right,float* up){
        forward[0]=1;forward[1]=forward[2]=0;right[0]=right[2]=0;right[1]=-1;up[0]=up[1]=0;up[2]=1;};
    engine.pfnRandomLong=[](int low,int){return low;};engine.pfnRandomFloat=[](float low,float){return low;};
    engine.pfnHookEvent=[](const char* path,void(*callback)(event_args_t*)){eventPath=path;k3Event=callback;};
    engine.pEventAPI=&ownershipEvents;engine.pEfxAPI=&ownershipEffects;
    ownershipEvents.EV_IsLocal=[](int index){return int(index==0);};ownershipEvents.EV_FindModelIndex=[](const char*){return 15;};
    ownershipEvents.EV_PlaySound=[](int owner,float*,int,const char*,float,float,int,int){++eventSounds;worldSoundOwner=owner;};
    ownershipEvents.EV_LocalPlayerViewheight=[](float* height){++localHeightReads;height[0]=height[1]=0;height[2]=77;};
    ownershipEvents.EV_SetUpPlayerPrediction=[](int,int){};
    ownershipEvents.EV_PushPMStates=[](){++predictionPushes;};ownershipEvents.EV_PopPMStates=[](){++predictionPops;};
    ownershipEvents.EV_SetSolidPlayers=[](int){};ownershipEvents.EV_SetTraceHull=[](int){};
    ownershipEvents.EV_PlayerTrace=[](float* start,float* end,int,int,pmtrace_t* trace){++eventTraces;
        std::copy_n(start,3,traceOrigin.begin());trace->fraction=1;std::copy_n(end,3,trace->endpos);};
    ownershipEvents.EV_GetPhysent=[](int){return &ownershipPhysical;};
    ownershipEffects.R_TempModel=[](float* origin,float*,float*,float,int,int)->TEMPENTITY*{
        ++eventShells;std::copy_n(origin,3,shellOrigin.begin());temps.emplace_back();return &temps.back();};
    ownershipEffects.R_DefaultSprite=[](float*,int,float)->TEMPENTITY*{temps.emplace_back();return &temps.back();};
    downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t* command,int run,double,unsigned){
        ++stockCalls;stockId=from->client.m_iId;stockRun=run;stockSelection=command->weaponselect;
        if(!stockId)return; // Matched stock client returns before state copy for ID0.
        *to=*from;
        if(stockAnimationCache&&(!localWeaponPrediction||from->client.health<=0||from->client.deadflag||!from->client.viewmodel))return;
        if(command->buttons&IN_ATTACK){--to->weapondata[0].m_iClip;to->client.weaponanim=90+stockId;
            if(stockAnimationCache&&stockId==29)to->client.weaponanim=6;
            StockAnimation(to->client.weaponanim,stockId,weaponView.curstate.body,run);}
        else if(stockAnimationCache&&(command->buttons&IN_ATTACK2)&&stockId==29)
        {to->client.weaponanim=5;StockAnimation(5,stockId,weaponView.curstate.body,run);}
        if(command->weaponselect>0&&command->weaponselect<LegacyWeaponLimit&&command->weaponselect!=stockId)
        {
            ++stockDeploys;to->client.m_iId=command->weaponselect;to->client.viewmodel=41;
            to->client.weaponanim=120+command->weaponselect;
            StockAnimation(to->client.weaponanim,to->client.m_iId,weaponView.curstate.body,run);
        }
        // In the matched binary EDI still identifies the source weapon after
        // weaponselect; both HUD getters are compared before force=1 output.
        if(stockAnimationCache&&run&&(cachedStockAnimation!=to->client.weaponanim||cachedStockWeapon!=stockId))
            StockAnimation(to->client.weaponanim,stockId,2,run,true);
        if(currentDuringStock){const int replacement=currentDuringStock;currentDuringStock=0;SelectCurrent(replacement);}
        if(roundTripDuringStock){roundTripDuringStock=false;SelectCurrent(197);SelectCurrent(29);}
    };
    InitializeK3();Expect(eventPath==k3::EventPath&&k3Event,"production K3 event hook captured");
}
}
int main()
{
    char program[]="switch-ownership",mode[]="--pickup-only";char* argv[]{program,mode};
    if(ExistingWeaponChecks(2,argv))return 1;
    // The reused initializer's MetaHook table was local. No subsequent test
    // uses it; clear the dangling pointer rather than inventing hook behavior.
    api=nullptr;now=300;view.health=100;view.spectator=0;hud=1;screenWidth=1280;screenHeight=720;
    EngineBoundary();ExtendedMatrix();StockBoundary();StockSelectionBoundary();CurrentCacheSurvives();SessionAndBadPackets();LateK3Events();
    StockCacheHandoff();StockCacheLifecycle();
    std::cout<<"Switch ownership "<<ownershipChecks<<" checks, "<<ownershipFailures<<" failures\n";
    return ownershipFailures?1:0;
}
