// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "buy_menu.h"
#include "custom/damage_refund_account.h"
#include "custom/gameplay_settings.h"
#include "scenario/core/game_rules_gate.h"
#include "weapons/primary_purchase.h"
#include "weapons/guest_cof_light_equipment.hpp"
#include "weapons/weapon_service.h"
#include "weapons/weapon_slots.h"
#include "weapons/weapons.h"
#include "weapons/wpn_cannon.h"
#include "weapons/melee_purchase.h"
#include "scenario/core/scenario_player_runtime.h"
#include "scenario/core/scenario_runtime.h"
#include "../scenario/custom/challenge_cycle.hpp"
#include "../weapons/melee_identity.hpp"
#include "../weapons/skull3.hpp"
#include "../buy_menu.hpp"
#include "../../guest/cof/catalog.hpp"
#include <algorithm>
#include <array>
#include <limits>

extern bool g_bClientPrintEnable;
// Matched host client.cpp2760/3882: keep the actual InternalCommand hookchain.
void EXT_FUNC InternalCommand(edict_t*,const char*,const char*);

namespace {
using namespace csol::buy;
namespace custom = csol::scenario::custom;
struct BlindBoxReward {
    const Item* item;
    std::uint32_t weight;
};
struct BlindBoxPool {
    std::array<BlindBoxReward, custom::MaximumBlindBoxEntries> rewards{};
    std::size_t count{};
};
using BlindBoxPools = std::array<BlindBoxPool, custom::BlindBoxCount>;
std::shared_ptr<const BlindBoxPools> blindBoxPools;
static_assert(custom::MaximumBlindBoxEntries * custom::MaximumBlindBoxWeight <= std::numeric_limits<int>::max());

struct Client {
    int userid{};
    EHANDLE player;
    std::uint64_t generation{};
    std::uint16_t request{};
    bool purchased{},busy{},delivering{},replaying{};
    Packet last{};
    // Original default selection is index0, but an empty rebuy record is FF.
    // Host IDs distinguish the concrete knife implementations from ID29.
    int selectedKnife=WEAPON_KNIFE,knife=-1;
};
std::array<Client,33> clients;
std::uint64_t epoch{};
std::uint64_t clientGeneration{};
int message{};
int hintMessage{};
bool sendingHint{};
bool ready{},equipmentCatalog{},arsenalCatalog{};
std::vector<std::string> resources;
void Send(const Packet& p, CBasePlayer* player=nullptr)
{
    if(!message)return;
    if(player)MESSAGE_BEGIN(MSG_ONE,message,nullptr,player->pev);else MESSAGE_BEGIN(MSG_INIT,message);
    for(auto b:Encode(p))WRITE_BYTE(b);MESSAGE_END();
}
bool Eligible(CBasePlayer* player)
{
    return ready && player && !player->has_disconnected && !(player->pev->flags&(FL_KILLME|FL_DORMANT|FL_FAKECLIENT|FL_SPECTATOR)) &&
        player->IsAlive() && player->m_iTeam==CT && CSOL_CanUseExtendedWeapons(player) && CSOL_ScenarioCanBuy();
}
int Price(CBasePlayer* player,const Item& item)
{
    // The stock item consumer discounts a helmet/vest already owned.
    if(item.id==1002)return player->pev->armorvalue>=100?HELMET_PRICE:
        player->m_iKevlar==ARMOR_VESTHELM?KEVLAR_PRICE:ASSAULTSUIT_PRICE;
    if (guest::cof::light::Find(item.id))
        return CSOL_ReadNightEquipmentPrice(item.id == guest::cof::light::Lantern.id);
    if(!IsWeapon(item) || item.id==WEAPON_KNIFE || CSOL_ExtendedWeapon(item.id))return item.extendedPrice;
    auto* info=GetWeaponInfo(item.id);return info?info->cost:0;
}
bool Available(CBasePlayer* player,const Item& item)
{
    if(guest::cof::arsenal::Find(item.id) && !arsenalCatalog)return false;
    if (guest::cof::light::Find(item.id))
    {
        if (!equipmentCatalog || !CSOL_CustomFeatureEnabled(custom::Feature::NightEquipment) ||
            !CSOL_CustomFeatureEnabled(custom::Feature::TimeCycle)) return false;
        const CSOLScenarioSdkScope scope;
        if (!scope.Current()) return false;
        custom::ChallengePhaseView phase;
        const auto runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
        return CSOL_ReadChallengePhase(runtime, phase) && phase.time.phase == custom::Phase::Night &&
            CSOL_CanReceiveLightEquipment(*player, item.id) && scope.Current();
    }
    if(!IsWeapon(item))return true;
    if(item.id==WEAPON_KNIFE || CSOL_ExtendedWeapon(item.id))return CSOL_CanDeliverScenarioWeapon(player,item.id);
    auto* info=GetWeaponInfo(item.id);
    return info&&info->entityName&&CanBuyWeaponByMaptype(player->m_iTeam,static_cast<WeaponIdType>(item.id),CSGameRules()->m_bMapHasVIPSafetyZone==TRUE);
}
bool Number(const char* text,std::uint16_t& n,bool allowZero=false)
{
    if(!text||!*text)return false;unsigned value=0;
    for(;*text;++text){if(*text<'0'||*text>'9')return false;value=value*10+unsigned(*text-'0');if(value>65535)return false;}
    if(!value&&!allowZero)return false;n=std::uint16_t(value);return true;
}
Client* State(CBasePlayer& player)
{
    const int index=player.entindex();
    if(index<1||index>=int(clients.size()))return nullptr;
    const EHANDLE handle(&player);
    const auto before=epoch;
    const int userid=GETPLAYERUSERID(player.edict());
    if(before!=epoch||handle.Get<CBaseEntity>()!=&player)return nullptr;
    auto& state=clients[index];
    if(state.userid!=userid||state.player.Get<CBaseEntity>()!=&player){
        state={userid,handle,++clientGeneration};
        // Host isolation: a new connection/map transaction must not inherit
        // a previous occupant's native snapshot before its first capture.
        player.m_rebuyStruct={};
    }
    return &state;
}
CBasePlayerWeapon* Held(CBasePlayer& player,int slot)
{
    auto* weapon=dynamic_cast<CBasePlayerWeapon*>(player.m_rgpPlayerItems[slot]);
    return weapon&&!(weapon->pev->flags&FL_KILLME)&&weapon->m_pPlayer==&player&&
        weapon->pev->owner==player.edict()?weapon:nullptr;
}
int Reserve(const CBasePlayer& player,const CBasePlayerWeapon& weapon)
{
    const int index=weapon.m_iPrimaryAmmoType;
    return index>=0&&index<MAX_AMMO_SLOTS?player.m_rgAmmo[index]:0;
}
int Grenades(const CBasePlayer& player,const char* name)
{
    const int index=CBasePlayer::GetAmmoIndex(name);
    return index>=0&&index<MAX_AMMO_SLOTS?player.m_rgAmmo[index]:0;
}
int PurchaseIdentity(const CBasePlayerWeapon& weapon)
{
    // MP1140DB58 saves A's purchase ID; the D grant jumps back to DBA4,
    // after that write. Both owned SKULL3 entities replay the paired purchase.
    return weapon.m_iId==csol::skull3::DualId?csol::skull3::Id:weapon.m_iId;
}
bool OwnsGun(CBasePlayer& player,int id)
{
    // MP1140D853..1140D8B0 scans every non-knife inventory chain. Compare
    // actual IDs: SKULL3's A remains linked while its D entity is active.
    for(int slot=0;slot<MAX_ITEM_TYPES;++slot){
        if(slot==KNIFE_SLOT)continue;
        for(auto* item=player.m_rgpPlayerItems[slot];item;item=item->m_pNext){
            const auto* gun=dynamic_cast<const CBasePlayerWeapon*>(item);
            if(gun&&gun->m_iId==id&&!(gun->pev->flags&FL_KILLME)&&
                gun->m_pPlayer==&player&&gun->pev->owner==player.edict())return true;
        }
    }
    return false;
}
int SnapshotValue(int value){return static_cast<std::int16_t>(value);}
void NativeCommand(CBasePlayer& player,const char* command)
{
    g_ReGameHookchains.m_InternalCommand.callChain(InternalCommand,player.edict(),command,"");
}

struct FinishDelivery {
    Client& state;
    std::uint64_t stamp, generation;
    ~FinishDelivery() { if (epoch == stamp && state.generation == generation) state.delivering = false; }
};

bool BlindBoxStockGun(const Item& item)
{
    return item.id != WEAPON_KNIFE && !CSOL_ExtendedWeapon(item.id);
}

bool BlindBoxRewardAvailable(CBasePlayer& player, const Item& item)
{
    if (RebuySlot(item) == 3)
    {
        const auto* held = Held(player, KNIFE_SLOT);
        if (held && held->m_iId == item.id) return false;
        if (item.id == WEAPON_KNIFE && player.HasRestrictItem(ITEM_KNIFE, ITEM_TYPE_BUYING)) return false;
    }
    else if (OwnsGun(player, item.id)) return false;
    return BlindBoxStockGun(item) ? CSOL_CanDeliverScenarioStockGun(player, item.id) :
        CSOL_CanDeliverScenarioWeapon(&player, item.id);
}

// Relative weights fit into the engine's signed RNG arguments. Accumulate
// locally so neither weapon offers nor spawn equipment allocate during a roll.
template<class AvailableReward>
const Item* RollWeapon(const BlindBoxPool& pool, int slot, const AvailableReward& available)
{
    if (!g_engfuncs.pfnRandomLong) return nullptr;
    std::array<BlindBoxReward, custom::MaximumBlindBoxEntries> eligible;
    std::size_t count{};
    std::uint32_t total{};
    for (std::size_t index = 0; index < pool.count; ++index)
    {
        const auto reward = pool.rewards[index];
        if ((!slot || RebuySlot(*reward.item) == slot) && available(*reward.item))
        {
            total += reward.weight;
            eligible[count++] = {reward.item, total};
        }
    }
    if (!total) return nullptr;
    const int roll = RANDOM_LONG(1, static_cast<int>(total));
    if (roll < 1 || roll > static_cast<int>(total)) return nullptr;
    return std::lower_bound(eligible.data(), eligible.data() + count, std::uint32_t(roll),
        [](const BlindBoxReward& reward, std::uint32_t value) { return reward.weight < value; })->item;
}

template<class Current, class Action>
bool Debit(CBasePlayer& player, Client& state, int price, const Current& sameBuyer, const Action& action)
{
    state.delivering = true;
    const FinishDelivery delivery{state, epoch, state.generation};
    custom::RefundAwardScope purchase(nullptr, {}, nullptr, nullptr, nullptr);
    const int before = player.m_iAccount;
    if (price) player.AddAccount(-price, RT_PLAYER_BOUGHT_SOMETHING, true);
    if (!sameBuyer()) return false;
    const int debit = before - player.m_iAccount;
    if (debit == price && action()) return true;
    if (debit > 0 && sameBuyer())
    {
#ifdef REGAMEDLL_API
        custom::RefundAccountScope account(&player);
        player.AddAccount_OrigFunc(debit, RT_NONE, false);
#else
        player.AddAccount(debit, RT_NONE, false);
#endif
    }
    return false;
}

// Zero selects all weapons in the slot. Blind boxes supply the IDs
// of their rewards, leaving other loaded magazines alone.
bool RefillAmmo(CBasePlayer& player, bool apply, std::array<int, 2> rewards = {})
{
    bool changed = false;
    bool reserveChanged = false;
    for (int slot : {PRIMARY_WEAPON_SLOT, PISTOL_SLOT})
    {
        const int reward = rewards[slot == PRIMARY_WEAPON_SLOT ? 0 : 1];
        if (reward < 0) continue;
        for (auto* item = player.m_rgpPlayerItems[slot]; item; item = item->m_pNext)
        {
            auto* weapon = dynamic_cast<CBasePlayerWeapon*>(item);
            if (!weapon || (weapon->pev->flags & FL_KILLME) ||
                weapon->m_pPlayer != &player || weapon->pev->owner != player.edict() ||
                (reward && PurchaseIdentity(*weapon) != reward)) continue;
            const int ammo = weapon->m_iPrimaryAmmoType;
            if (!weapon->pszAmmo1() || ammo <= 0 || ammo >= MAX_AMMO_SLOTS) continue;
            const int clip = weapon->iMaxClip();
            const int capacity = CSOL_ClipAmmo(weapon) ? 0 : weapon->iMaxAmmo1();
            const bool fillClip = weapon->m_iClip >= 0 && weapon->m_iClip < clip;
            const bool fillReserve = capacity > 0 && player.m_rgAmmo[ammo] < capacity;
            if (!fillClip && !fillReserve) continue;
            if (!apply) return true;
            if (fillClip) weapon->m_iClip = clip;
            if (fillReserve) player.m_rgAmmo[ammo] = capacity;
            changed = true;
            reserveChanged |= fillReserve;
        }
    }
    if (reserveChanged) player.TabulateAmmo();
    return changed;
}

template<class Current, class SameBuyer>
void BuyAmmoRefill(CBasePlayer& player, Client& state, int quotedPrice,
    const Current& current, const SameBuyer& sameBuyer)
{
    const auto allowed = [&] {
        return current() && Eligible(&player) && current() && player.CanPlayerBuy(false) && current();
    };
    if (!allowed())
    {
        if (current()) ClientPrint(player.pev, HUD_PRINTTALK, "请在购买区购买弹药。\n");
        return;
    }
    const auto price = CSOL_ReadAmmoRefillPrice();
    if (!price || *price != quotedPrice)
    {
        ClientPrint(player.pev, HUD_PRINTTALK, "弹药报价已失效，请重新打开购买系统。\n");
        return;
    }
    if (!RefillAmmo(player, false))
    {
        ClientPrint(player.pev, HUD_PRINTTALK, "当前武器无需补弹。\n");
        return;
    }
    if (player.m_iAccount < *price)
    {
        ClientPrint(player.pev, HUD_PRINTCENTER, "#Not_Enough_Money");
        return;
    }
    const bool filled = Debit(player, state, *price, sameBuyer, [&] {
        return allowed() && CSOL_ReadAmmoRefillPrice() == price && RefillAmmo(player, true);
    });
    if (!filled || !current()) return;
    player.BuildRebuyStruct();
    if (current()) ClientPrint(player.pev, HUD_PRINTTALK, "主副武器弹药已补满。\n");
}

bool DeliverReward(CBasePlayer& player, const Item& item)
{
    return BlindBoxStockGun(item) ? CSOL_DeliverScenarioStockGun(player, item.id) :
        CSOL_DeliverScenarioWeapon(&player, item.id);
}

template<class Current, class SameBuyer>
void BuyBlindBox(CBasePlayer& player, Client& state, custom::BlindBox box, int quotedPrice,
    const Current& current, const SameBuyer& sameBuyer)
{
    // The blind-box shop removes the buy-zone requirement for this transaction.
    const auto allowed = [&] {
        return current() && Eligible(&player) && current() && !player.m_bIsVIP && !CSGameRules()->m_bCTCantBuy;
    };
    if (!allowed()) return;
    const auto prices = CSOL_ReadBlindBoxPrices();
    const auto pools = blindBoxPools;
    const auto index = static_cast<std::size_t>(box);
    if (!prices || !pools)
    {
        ClientPrint(player.pev, HUD_PRINTTALK, "武器盲盒当前不可购买。\n");
        return;
    }
    const auto offerCurrent = [&] { return blindBoxPools == pools && CSOL_ReadBlindBoxPrices() == prices; };
    const int price = (*prices)[index];
    if (price != quotedPrice)
    {
        ClientPrint(player.pev, HUD_PRINTTALK, "盲盒价格已变化，请重新打开购买系统。\n");
        return;
    }
    if (player.m_iAccount < price)
    {
        ClientPrint(player.pev, HUD_PRINTCENTER, "#Not_Enough_Money");
        return;
    }

    const bool pair = box == custom::BlindBox::Csol;
    const auto available = [&](const Item& item)
    {
        return current() && BlindBoxRewardAvailable(player, item);
    };
    const auto& pool = (*pools)[index];
    const auto* primary = RollWeapon(pool, pair ? 2 : 0, available);
    const auto* secondary = pair && primary ? RollWeapon(pool, 1, available) : nullptr;
    if (!current()) return;
    if (!primary || (pair && !secondary))
    {
        ClientPrint(player.pev, HUD_PRINTTALK, "该盲盒当前没有可领取的武器组合。\n");
        return;
    }
    if (!allowed() || !offerCurrent() || player.m_iAccount < price) return;
    const Item* melee{};
    const bool granted = Debit(player, state, price, sameBuyer, [&]
    {
        if (!allowed() || !offerCurrent() || !available(*primary) || !current()) return false;
        if (secondary)
        {
            if (!available(*secondary) || !current()) return false;
            auto pairCurrent = [&] { return allowed() && offerCurrent(); };
            if (!CSOL_DeliverBlindBoxPair(player, primary->id, secondary->id,
                [](void* context) { return (*static_cast<decltype(pairCurrent)*>(context))(); }, &pairCurrent))
                return false;
            RefillAmmo(player, true, {primary->id, secondary->id});
            // The paid pair has committed. Draw a configured knife as a bonus.
            if (allowed() && offerCurrent())
            {
                const auto* bonus = RollWeapon(pool, 3, available);
                if (bonus && allowed() && offerCurrent() && DeliverReward(player, *bonus))
                    melee = bonus;
            }
        }
        else
        {
            if (!DeliverReward(player, *primary)) return false;
            if (current())
            {
                const int slot = RebuySlot(*primary);
                if (slot == 2) RefillAmmo(player, true, {primary->id, -1});
                if (slot == 1) RefillAmmo(player, true, {-1, primary->id});
            }
        }
        return true;
    });
    if (!current()) return;
    if (!granted)
    {
        ClientPrint(player.pev, HUD_PRINTTALK, "盲盒购买失败。\n");
        return;
    }
    player.BuildRebuyStruct();
    if (!current()) return;
    char notice[200];
    if (melee)
        Q_snprintf(notice, sizeof(notice), "盲盒获得 %s、%s 和 %s\n", primary->name, secondary->name, melee->name);
    else if (secondary)
        Q_snprintf(notice, sizeof(notice), "盲盒获得 %s 和 %s\n", primary->name, secondary->name);
    else
        Q_snprintf(notice, sizeof(notice), "盲盒获得 %s\n", primary->name);
    ClientPrint(player.pev, HUD_PRINTTALK, notice);
}

template<class Current, class SameBuyer>
Result Purchase(CBasePlayer& player, Client& state, const Item& item,
    const Current& current, const SameBuyer& sameBuyer, int quotedPrice = -1)
{
    if (!IsDirectPurchaseItem(item) || !current() || !Eligible(&player) || !current()) return Result::Unavailable;
    if(!player.CanPlayerBuy(false)||!current())return Result::Unavailable;
    // The native weapon buyer already rejects owned guns. Its extension
    // delivery counterpart can replace them, so restore the original common
    // purchase gate here, before availability, balance and any debit/grant.
    // MP1140D916..1140D959 returns failure; knife selection has its own tail.
    if(IsWeapon(item)&&CSOL_ExtendedWeapon(item.id)&&item.slot!=WeaponSlot::Melee&&OwnsGun(player,item.id)){
        if(g_bClientPrintEnable)ClientPrint(player.pev,HUD_PRINTCENTER,"#Cstrike_Already_Own_Weapon");
        return Result::Failed;
    }
    if(!Available(&player,item)||!current())return Result::Unavailable;
    const int price=Price(&player,item);
    if (quotedPrice >= 0 && price != quotedPrice) return Result::Unavailable;
    if(!IsAmmunition(item.id)&&player.m_iAccount<price)return Result::Insufficient;
    if (guest::cof::light::Find(item.id))
    {
        const bool granted = Debit(player, state, price, sameBuyer, [&] {
            return current() && Eligible(&player) && current() && player.CanPlayerBuy(false) &&
                current() && Available(&player, item) && current() && Price(&player, item) == price &&
                CSOL_GiveLightEquipment(player, item.id);
        });
        return granted ? Result::Success : Result::Failed;
    }
    // MP+192C is independent from the replay guard. Internal delivery must
    // not let a hook snapshot the temporary inventory before the common tail.
    state.delivering=true;
    const FinishDelivery finishDelivery{state,epoch,state.generation};
    bool success=false;
    if(IsAmmunition(item.id)){
        const int slot=item.id==2001?PRIMARY_WEAPON_SLOT:PISTOL_SLOT;
        const auto ammoState=[&](){
            std::array<int,MAX_AMMO_SLOTS+2> ammo{};
            std::copy_n(player.m_rgAmmo,MAX_AMMO_SLOTS,ammo.begin());
            for(int i=0;i<2;++i){
                const auto* gun=Held(player,i==0?PRIMARY_WEAPON_SLOT:PISTOL_SLOT);
                ammo[MAX_AMMO_SLOTS+i]=gun?gun->m_iClip:-1;
            }
            return ammo;
        };
        bool first=true;
        while(current()&&Eligible(&player)&&current()){
            const auto before=ammoState();
            const bool bought=BuyAmmo(&player,slot,first);
            if(!current())return Result::Unavailable;
            const bool gained=ammoState()!=before;
            success|=gained;
            // primammo/secammo buy to capacity; a no-progress hook stops.
            if(!bought||!gained)break;
            first=false;
        }
    }else if(IsStockEquipment(item.id)){
        const int before=player.m_iAccount;
        constexpr int slots[]{MENU_SLOT_ITEM_VEST,MENU_SLOT_ITEM_VESTHELM,MENU_SLOT_ITEM_HEGREN,MENU_SLOT_ITEM_FLASHGREN,MENU_SLOT_ITEM_SMOKEGREN};
        BuyItem(&player,slots[item.id-1001]);
        if(current())success=player.m_iAccount<before;
    }else if(item.id!=WEAPON_KNIFE&&!CSOL_ExtendedWeapon(item.id)){
        success=BuyWeaponByWeaponID(&player,static_cast<WeaponIdType>(item.id))!=nullptr;
    }else{
        const int before=player.m_iAccount;
        player.AddAccount(-price,RT_PLAYER_BOUGHT_SOMETHING,true);
        if(!current())return Result::Unavailable;
        const int debit=before-player.m_iAccount;
        if(debit==price&&Eligible(&player))success=CSOL_DeliverScenarioWeapon(&player,item.id);
        if(!current())return Result::Unavailable;
        if(!success&&debit>0&&player.m_iAccount<=std::numeric_limits<int>::max()-debit)
            player.AddAccount(debit,RT_NONE,false);
    }
    return success?Result::Success:Result::Failed;
}

template<class Current>
void Replay(CBasePlayer& player,Client& state,const Current& current)
{
    if(state.replaying||state.delivering||player.m_bIsInRebuy)return;
    state.replaying=true;
    const bool printing=g_bClientPrintEnable;
    g_bClientPrintEnable=false;
    struct FinishReplay {
        Client& state;std::uint64_t stamp,generation;bool printing;
        ~FinishReplay(){if(epoch==stamp&&state.generation==generation)state.replaying=false;g_bClientPrintEnable=printing;}
    }finish{state,epoch,state.generation,printing};
    const auto allowed=[&](){
        return current()&&Eligible(&player)&&current()&&player.CanPlayerBuy(false)&&current();
    };
    const auto buy=[&](int id){
        if(!allowed())return;
        if(const auto* item=Find(id))Purchase(player,state,*item,current,current);
        else if(id==WEAPON_SHIELDGUN)NativeCommand(player,"shield");
    };
    // MP114E5030 fixed order. Each normal purchase settles independently;
    // an ordinary failure does not cancel later items or debit a whole order.
    if(!allowed())return;
    if(player.m_rebuyStruct.m_primaryWeapon)buy(player.m_rebuyStruct.m_primaryWeapon);
    if(!allowed())return;
    if(const auto* gun=Held(player,PRIMARY_WEAPON_SLOT))
        if(player.m_rebuyStruct.m_primaryAmmo>Reserve(player,*gun))buy(2001);
    if(!allowed())return;
    if(player.m_rebuyStruct.m_defuser&&!player.m_bHasDefuser)
        NativeCommand(player,"defuser");
    if(!allowed())return;
    if(player.m_rebuyStruct.m_armor&&player.m_iKevlar<player.m_rebuyStruct.m_armor)
        buy(player.m_rebuyStruct.m_armor==ARMOR_KEVLAR?1001:1002);
    const auto grenades=[&](int saved,const char* name,int id){
        if(!allowed())return;
        const int count=saved-Grenades(player,name);
        for(int i=0;i<count;++i){if(!allowed())return;buy(id);}
    };
    if(!allowed())return;
    grenades(player.m_rebuyStruct.m_heGrenade,"HEGrenade",1003);
    if(!allowed())return;
    grenades(player.m_rebuyStruct.m_flashbang,"Flashbang",1004);
    if(!allowed())return;
    grenades(player.m_rebuyStruct.m_smokeGrenade,"SmokeGrenade",1005);
    if(!allowed())return;
    if(player.m_rebuyStruct.m_secondaryWeapon)buy(player.m_rebuyStruct.m_secondaryWeapon);
    if(!allowed())return;
    if(const auto* gun=Held(player,PISTOL_SLOT))
        // Original tknife v218=true; its host total-clip representation does
        // not turn it into the separate, unsupported original v21C branch.
        if(player.m_rebuyStruct.m_secondaryAmmo>Reserve(player,*gun)||gun->m_iId==CSOL_WEAPON_TKNIFEEX2)buy(2002);
    if(!allowed())return;
    if(player.m_rebuyStruct.m_nightVision&&!player.m_bHasNightVision)
        NativeCommand(player,"nvgs");
    if(!allowed())return;
    if(state.knife!=-1)buy(state.knife);
    // The real knife delivery executes its metadata common tail even when
    // retaining the same entity, preserving attacks and deployment state.
    if(!allowed())return;
    // MP114E58D8 calls the separate paint tail even for absent/failed knife
    // selection. No parts, selected identity or attack state is changed.
    if(!guest::cof::arsenal::Find(state.selectedKnife))CSOL_ScenarioRefreshMeleePaint(player,state.selectedKnife);
}
}
void CSOL_ResetBuyMenu(){++epoch;ready=false;equipmentCatalog=false;arsenalCatalog=false;hintMessage=0;clients={};resources.clear();}
bool CSOL_BuildRebuyStruct(CBasePlayer* player)
{
    CSOLScenarioSdkScope scope;
    if(!scope.Scenario())return false;
    if(!scope.Current()||!player||player->has_disconnected)return true;
    auto* state=State(*player);
    if(!scope.Current()||!state||state->replaying||state->delivering||player->m_bIsInRebuy)return true;
    RebuyStruct snapshot{};
    if(const auto* gun=Held(*player,PRIMARY_WEAPON_SLOT)){
        snapshot.m_primaryWeapon=SnapshotValue(PurchaseIdentity(*gun));
        snapshot.m_primaryAmmo=SnapshotValue(Reserve(*player,*gun));
    }else if(player->HasShield())snapshot.m_primaryWeapon=WEAPON_SHIELDGUN;
    if(const auto* gun=Held(*player,PISTOL_SLOT)){
        snapshot.m_secondaryWeapon=SnapshotValue(PurchaseIdentity(*gun));
        snapshot.m_secondaryAmmo=SnapshotValue(Reserve(*player,*gun));
    }
    snapshot.m_heGrenade=SnapshotValue(Grenades(*player,"HEGrenade"));
    snapshot.m_flashbang=SnapshotValue(Grenades(*player,"Flashbang"));
    snapshot.m_smokeGrenade=SnapshotValue(Grenades(*player,"SmokeGrenade"));
    snapshot.m_defuser=player->m_bHasDefuser;
    snapshot.m_nightVision=player->m_bHasNightVision;
    snapshot.m_armor=static_cast<ArmorType>(SnapshotValue(player->m_iKevlar));
    player->m_rebuyStruct=snapshot;
    state->knife=state->selectedKnife;
    return true;
}
void CSOL_RebuyMeleeSelected(CBasePlayer& player,int weaponId)
{
    const auto* item=Find(weaponId);
    if(!item || item->slot!=WeaponSlot::Melee)return;
    CSOLScenarioSdkScope scope;
    if(!scope.Scenario()||!scope.Current())return;
    if(auto* state=State(player);scope.Current()&&state){
        state->selectedKnife=weaponId;
        state->knife=weaponId;
    }
}
bool CSOL_BlocksDirectWeaponPurchase(int weaponId)
{
    const auto* item = csol::buy::Find(weaponId);
    return item && !csol::buy::IsDirectPurchaseItem(*item) && CSOL_UsesScenarioRules();
}

bool CSOL_Rebuy(CBasePlayer* player)
{
    if(!CSOL_UsesScenarioRules())return false;
    return !player||CSOL_BuyMenuCommand(player->edict(),"cl_rebuy");
}
void CSOL_BuyZoneHint(CBasePlayer* player,bool inside)
{
    CSOLScenarioSdkScope scope;
    if(sendingHint||!ready||!hintMessage||!scope.Scenario()||!scope.Current()||!player||
        player->has_disconnected||(player->pev->flags&(FL_KILLME|FL_DORMANT|FL_FAKECLIENT)))return;
    const EHANDLE handle(player);
    if(inside&&(!Eligible(player)||!player->CanPlayerBuy(false)))return;
    if(!scope.Current()||handle.Get<CBaseEntity>()!=player)return;
    const int id=hintMessage;
    sendingHint=true;
    MESSAGE_BEGIN(MSG_ONE,id,nullptr,player->pev);
    WRITE_BYTE(inside?16:17);
    MESSAGE_END();
    sendingHint=false;
}
bool CSOL_CanBuyScenarioNativeWeapon(int playerTeam,int weaponId)
{
    // Scenario participants remain CT. The T/CT tabs classify equipment;
    // choosing a T weapon must not temporarily change authoritative team state.
    const auto* item=csol::buy::Find(weaponId);
    return CSOL_UsesScenarioRules()&&playerTeam==CT&&item&&csol::buy::IsWeapon(*item)&&!CSOL_ExtendedWeapon(item->id);
}
bool CSOL_PrecacheBuyMenu()
{
    CSOL_ResetBuyMenu();if(!CSOL_UsesScenarioRules())return false;
    const auto loadMenu=[](const char* path,bool withEquipment){
        int length{};
        auto* file=LOAD_FILE_FOR_ME(path,&length);
        if(!file)return false;
        try{
            const auto menu=ParsePresentation({reinterpret_cast<const char*>(file),std::size_t(length)});
            if(withEquipment){
                for(const auto& item:{guest::cof::light::Lantern,guest::cof::light::Flare})
                    if(!menu.images.contains(item.id)||!menu.weaponInfo.items.contains(item.id))
                        throw std::runtime_error("missing light equipment card");
            }
            const auto files=Resources(menu);
            if (path == ArsenalPresentationResource) {
                for (const auto& item : guest::cof::arsenal::Items)
                    if (!menu.images.contains(item.id) || !menu.weaponInfo.items.contains(item.id))
                        throw std::runtime_error("missing CoF weapon card");
                for (const auto& resource : files)
                    if (GET_FILE_SIZE(resource.c_str()) <= 0)
                        throw std::runtime_error("missing CoF menu resource " + resource);
            }
            resources.insert(resources.end(),files.begin(),files.end());
            if(withEquipment)resources.emplace_back(path);
        }catch(const std::exception& e){FREE_FILE(file);ALERT(at_error,"CSOL buy menu %s: %s\n",path,e.what());return false;}
        FREE_FILE(file);return true;
    };
    if(!loadMenu(PresentationResource,false))return false;
    equipmentCatalog=g_engfuncs.pfnGetFileSize && GET_FILE_SIZE(EquipmentPresentationResource)>0;
    if(equipmentCatalog && !loadMenu(EquipmentPresentationResource,true))return false;
    arsenalCatalog=equipmentCatalog && GET_FILE_SIZE(ArsenalPresentationResource)>0 &&
        loadMenu(ArsenalPresentationResource,true);
    std::sort(resources.begin(),resources.end());
    resources.erase(std::unique(resources.begin(),resources.end()),resources.end());
    for(const auto& path:resources)
        if(!g_engfuncs.pfnGetFileSize || GET_FILE_SIZE(path.c_str())<=0)
        {ALERT(at_error,"CSOL buy resource missing: %s\n",path.c_str());return false;}
    for(const auto& path:resources)PRECACHE_GENERIC(const_cast<char*>(path.c_str()));
    if(!message)message=REG_USER_MSG(csol::buy::Message,static_cast<int>(csol::buy::MessageBytes));
    hintMessage=REG_USER_MSG("ZBSVer2",-1);
    ready=message>=64&&message<=255&&hintMessage>=64&&hintMessage<=255;
    if(ready)Send({Kind::Capability,0,std::uint16_t(arsenalCatalog?ArsenalPresentationRevision:equipmentCatalog?EquipmentPresentationRevision:0)});return ready;
}
void CSOL_PrepareBlindBoxPools(const custom::Snapshot& settings)
{
    constexpr std::array<WeaponSource, custom::BlindBoxCount> sources{
        WeaponSource::Cof, WeaponSource::CounterStrike, WeaponSource::Csol
    };
    auto prepared = std::make_shared<BlindBoxPools>();
    for (const auto& box : custom::BlindBoxes())
    {
        const auto index = static_cast<std::size_t>(box.id);
        auto& pool = (*prepared)[index];
        for (const auto& entry : settings.Pool(box.id))
        {
            if (!entry.weight) continue;
            const auto* item = Find(entry.weapon);
            if (!item || !IsWeapon(*item) || item->source != sources[index]) continue;
            pool.rewards[pool.count++] = {item, entry.weight};
        }
    }
    blindBoxPools = std::move(prepared);
}

bool CSOL_BuyMenuCommand(edict_t* entity,const char* command)
{
    if(!command)return false;
    const bool replay=!Q_strcmp(command,"cl_rebuy")||!Q_strcmp(command,"rebuy_in");
    const bool blind=!Q_strcmp(command,"csol_blindbox");
    const bool refill = !Q_strcmp(command, "csol_ammo_refill");
    const bool light = !Q_strcmp(command, "csol_light");
    if(!replay&&!blind&&!light&&!refill&&Q_strcmp(command,"csol_buy_menu")&&Q_strcmp(command,"csol_buy"))return false;
    CSOLScenarioSdkScope scope;
    if(!scope.Scenario())return !replay;
    if(!scope.Current()||!entity||entity->free)return true;
    auto* player=dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(entity));if(!player)return true;
    const int index=player->entindex();if(index<1||index>=int(clients.size()))return true;
    const bool catalog=!Q_strcmp(command,"csol_buy_menu");std::uint16_t request{},id{};
    std::optional<custom::BlindBox> box;
    const Item* lightOffer = nullptr;
    if (light) {
        if (CMD_ARGC() != 3 || !(lightOffer = Find(CMD_ARGV(1))) ||
            !guest::cof::light::Find(lightOffer->id) || !Number(CMD_ARGV(2), id, true)) return true;
    } else if(blind){
        if(CMD_ARGC()!=3 || !(box=custom::FindBlindBox(CMD_ARGV(1))) || !Number(CMD_ARGV(2),id,true))return true;
    }else if (refill) {
        if (CMD_ARGC() != 2 || !Number(CMD_ARGV(1), id, true)) return true;
    }else if(!replay&&(CMD_ARGC()!=(catalog?2:3)||!Number(CMD_ARGV(1),request)||(!catalog&&!Number(CMD_ARGV(2),id))))return true;
    auto* found=State(*player);if(!scope.Current()||!found)return true;
    auto& state=*found;const int userid=state.userid;const auto stamp=epoch;const auto client=state.generation;
    if(state.busy)return true;state.busy=true;
    struct Finish {Client& c;int user;std::uint64_t stamp,generation;~Finish(){if(epoch==stamp&&c.userid==user&&c.generation==generation)c.busy=false;}}finish{state,userid,stamp,client};
    const EHANDLE handle(player);
    const int spawns=player->m_iNumSpawns;
    const auto authority=CSOL_ScenarioPlayerAuthority(scope.EntityAccess().get());
    const auto life=authority?authority->Find(handle):nullptr;
    if(!scope.Current() || !life)return true;
    const auto generation=life->generation;
    const auto nativeSpawn=life->nativeSpawnSerial;
    const auto sameBuyer=[&](){
        if(epoch!=stamp||state.generation!=client||!scope.Current() || state.userid!=userid || state.player.Get<CBaseEntity>()!=player || handle.Get<CBaseEntity>()!=player)return false;
        const int now=GETPLAYERUSERID(handle.Get());
        return epoch==stamp&&state.generation==client&&scope.Current() && state.userid==userid && state.player.Get<CBaseEntity>()==player && handle.Get<CBaseEntity>()==player &&
            now==userid && !player->has_disconnected && !(player->pev->flags&FL_KILLME) &&
            player->m_iNumSpawns==spawns && authority->Active() && authority->Find(handle)==life &&
            life->generation==generation && life->nativeSpawnSerial==nativeSpawn;
    };
    const auto runtime = CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
    if (!runtime || !runtime->session) return true;
    const auto round = runtime->session->Map().RoundGeneration();
    const auto current = [&] {
        return sameBuyer() && player->IsAlive() && runtime->session &&
            runtime->session->Map().RoundGeneration() == round;
    };
    if (!Eligible(player) || !current() ||
        (!blind && !light && !refill && !player->CanPlayerBuy(false)) || !current()) {
        if (!replay && !blind && !light && !refill && current())
            Send({Kind::Result,request,id,0,std::max(0,player->m_iAccount),Result::Unavailable,2},player);
        return true;
    }
    if (refill)
    {
        BuyAmmoRefill(*player, state, id, current, sameBuyer);
        return true;
    }
    if(box){BuyBlindBox(*player,state,*box,id,current,sameBuyer);return true;}
    if (lightOffer)
    {
        const auto result = Purchase(*player, state, *lightOffer, current, sameBuyer, id);
        if (!current()) return true;
        switch (result)
        {
        case Result::Success:
        {
            char notice[96];
            Q_snprintf(notice, sizeof(notice), "已购买 %s。\n", lightOffer->name);
            ClientPrint(player->pev, HUD_PRINTTALK, notice);
            break;
        }
        case Result::Insufficient:
            ClientPrint(player->pev, HUD_PRINTCENTER, "#Not_Enough_Money");
            break;
        case Result::Unavailable:
            ClientPrint(player->pev, HUD_PRINTTALK, "照明装备暂不可购买。请在永夜的购买区重新打开菜单并检查库存。\n");
            break;
        default:
            ClientPrint(player->pev, HUD_PRINTTALK, "照明装备购买失败。\n");
            break;
        }
        return true;
    }
    if(replay){Replay(*player,state,current);return true;}
    if(catalog){
        state.request=request;state.purchased=false;
        player->m_iMenu=Menu_OFF;
        Send({Kind::Begin,request,0,0,player->m_iAccount,Result::Available,2},player);
        for(const auto& item:Catalog()){
            if (!IsDirectPurchaseItem(item)) continue;
            if(!equipmentCatalog && guest::cof::light::Find(item.id))continue;
            if(!arsenalCatalog && guest::cof::arsenal::Find(item.id))continue;
            if(!current())return true;
            const bool available = Available(player, item);
            if(!current())return true;
            Send({Kind::Item,request,item.id,Price(player,item),player->m_iAccount,
                available?Result::Available:Result::Unavailable,2},player);
        }
        if(current())Send({Kind::End,request,0,0,player->m_iAccount,Result::Available,2},player);return true;
    }
    if(state.request!=request)return true;
    if(state.purchased){Send(state.last,player);return true;}
    const auto* item=Find(id);if(!item)return true;
    if (!IsDirectPurchaseItem(*item))
    {
        Send({Kind::Result, request, id, 0, player->m_iAccount, Result::Unavailable, 2}, player);
        return true;
    }
    const int price=Price(player,*item);
    const Result result=Purchase(*player,state,*item,current,sameBuyer);
    if(!current())return true;
    // Original114152DE/1141092C rebuild actual equipment even after a
    // normal failed purchase, once the command reached this admitted tail.
    player->BuildRebuyStruct();
    if(!current())return true;
    state.last={Kind::Result,request,id,price,player->m_iAccount,result,2};state.purchased=result==Result::Success;
    Send(state.last,player);return true;
}
