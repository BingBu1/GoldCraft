// SPDX-License-Identifier: GPL-3.0-or-later
// The original lifecycle fixture supplies explicit engine/effect boundaries.
// Inventory insertion, removal and weapon-box packing run actual SDK bodies.
#include "shared-glock-sdk.inc"

namespace pickup {
unsigned received{},respawns{},switches{},resets{};
#if COF_FULL_TOUCH
unsigned removed{},sounds{},pairedCalls{};
bool restricted{};
#endif
}
bool CSOL_IsMeleeReplacement(CBasePlayer&,CBasePlayerItem&);
void CSOL_RemoveBeamWeapon(CBasePlayerItem*);
#if COF_FULL_TOUCH
bool CSOL_TouchWeaponGroup(CWeaponBox&,CBasePlayer&);
#endif
#include "pickup-rules.inc"
#include "pickup-native.inc"

void CBasePlayer::ResetAutoaim(){Check(this==&player);}
void CBasePlayer::ResetMaxSpeed(){Check(this==&player);++pickup::resets;}
int CBasePlayer::SwitchWeapon(CBasePlayerItem*){std::abort();}
bool CSOL_IsMeleeReplacement(CBasePlayer& p,CBasePlayerItem& item)
{Check(&p==&player&&(item.m_iId==896||item.m_iId==WEAPON_USP));return false;}
void CSOL_RemoveBeamWeapon(CBasePlayerItem* item){Check(item==&weapon);}
extern "C" int A_strcmp(const char* a,const char* b){return std::strcmp(a,b);}

#if COF_FULL_TOUCH
bool CSOL_TouchWeaponGroup(CWeaponBox& box,CBasePlayer& p)
{Check(&p==&player);++pickup::pairedCalls;return false;}
bool CBasePlayer::HasRestrictItem(ItemID item,ItemRestType type)
{Check(this==&player&&(static_cast<int>(item)==896||item==ITEM_USP)&&type==ITEM_TYPE_TOUCHED);return pickup::restricted;}
void UTIL_Remove(CBaseEntity* entity)
{Check(dynamic_cast<CWeaponBox*>(entity)!=nullptr);++pickup::removed;entity->pev->flags|=FL_KILLME;}
bool CWeaponBox::GiveAmmoToPlayer(CBasePlayer*,CBasePlayerWeapon*,int,const char*,int,CBasePlayerItem**)
{std::abort();}
// The target is a human picking up a Glock. C4, grenade, Bot, round-management
// and voice activity are explicit boundaries and abort if unexpectedly reached.
CVoiceGameMgr::CVoiceGameMgr() {}
CVoiceGameMgr::~CVoiceGameMgr() {}
RewardAccount CHalfLifeMultiplay::m_rgRewardAccountRules[RR_END]{};
bool CCSBotManager::IsWeaponUseable(CBasePlayerItem*) const{std::abort();}
void CCSBotManager::SetLooseBomb(CBaseEntity*){std::abort();}
bool CBasePlayer::HintMessage(const char*,BOOL,BOOL){std::abort();}
void CBasePlayer::SetBombIcon(BOOL){std::abort();}
int CBasePlayerItem::iMaxAmmo1() const{std::abort();}
CBasePlayerItem* CBasePlayer::GetItemById(WeaponIdType){std::abort();}
void ClientPrint(entvars_t*,int,const char*,const char*,const char*,const char*,const char*){std::abort();}
void UTIL_LogPrintf(const char*,...){std::abort();}
CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity*,const char*){std::abort();}
int SENTENCEG_Lookup(const char*,char (&)[32]){std::abort();}

struct ControlledStockPistol final:CBasePlayerItem
{
    unsigned additions{};
    int iItemSlot() override{return PISTOL_SLOT;}
    int AddToPlayer(CBasePlayer* owner) override
    {
        // The native stock class itself is outside this guest fixture; the
        // actual box/list and AddPlayerItem code must call its consumer once.
        Check(owner==&player&&++additions==1);m_pPlayer=owner;return TRUE;
    }
};

void MixedBoxCase(PickupRules& rules)
{
    Initialize();g_pGameRules=&rules;player.m_bIsVIP=player.m_bShieldDrawn=false;
    pickup::restricted=false;extendedAllowed=true;
    weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});weapon.Precache();
    Check(weapon.Bind(player,prepared,backends)==GlockStatus::Ready);
    weapon.AttachToPlayer(&player);player.m_rgpPlayerItems[PISTOL_SLOT]=&weapon;
    weapon.m_iDefaultAmmo=0;weapon.BeforeWeaponDrop();
    edict_t boxEdict{},stockEdict{};CWeaponBox box{};ControlledStockPistol stock{};
    box.pev=&boxEdict.v;boxEdict.v.pContainingEntity=&boxEdict;boxEdict.pvPrivateData=&box;
    stock.pev=&stockEdict.v;stockEdict.v.pContainingEntity=&stockEdict;stockEdict.pvPrivateData=&stock;stock.m_iId=WEAPON_USP;
    std::string strings;
    const int end=int(weapon.pev->classname)+int(std::strlen(STRING(weapon.pev->classname)))+1;
    strings.assign(globals.pStringBase,globals.pStringBase+end);
    stock.pev->classname=int(strings.size());strings.append("weapon_usp",10);globals.pStringBase=strings.data();
    Check(box.PackWeapon(&weapon)&&box.PackWeapon(&stock));
    box.m_rgpPlayerItems[PISTOL_SLOT]=&weapon;weapon.m_pNext=&stock;stock.m_pNext=nullptr;
    box.pev->flags=FL_ONGROUND;box.SetTouch(&CWeaponBox::Touch);
    pickup::removed=pickup::sounds=0;extendedAllowed=false;
    g_engfuncs.pfnEmitSound=[](edict_t*,int,const char*,float,float,int,int){++pickup::sounds;};
    const auto received=pickup::received;std::cout<<"Native mixed stock/guest box\n";
    box.Touch(&player);
    Check(stock.additions==1&&stock.m_pPlayer==&player&&player.m_rgpPlayerItems[PISTOL_SLOT]==&stock);
    Check(box.m_rgpPlayerItems[PISTOL_SLOT]==&weapon&&weapon.m_pNext==nullptr&&weapon.m_pPlayer==nullptr);
    Check(stock.pev->owner==player.edict()&&pickup::removed==0&&pickup::sounds==1&&pickup::received==received+1);
    extendedAllowed=true;
}

void FullTouchCases(PickupRules& rules)
{
    for(int rejection=0;rejection<=8;++rejection)
    {
        Initialize();g_pGameRules=&rules;
        player.m_bIsVIP=false;player.m_bShieldDrawn=false;pickup::restricted=false;extendedAllowed=true;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});weapon.Precache();
        Check(weapon.Bind(player,prepared,backends)==GlockStatus::Ready);
        weapon.AttachToPlayer(&player);Check(weapon.Deploy());globals.time=10;
        weapon.m_iClip=75;weapon.m_iDefaultAmmo=0;player.m_rgAmmo[1]=40;
        player.m_rgpPlayerItems[PISTOL_SLOT]=&weapon;weapon.BeforeWeaponDrop();
        edict_t boxEdict{};CWeaponBox box{};box.pev=&boxEdict.v;
        boxEdict.v.pContainingEntity=&boxEdict;boxEdict.pvPrivateData=&box;
        Check(box.PackWeapon(&weapon));box.pev->flags=FL_ONGROUND;box.SetTouch(&CWeaponBox::Touch);
        CBasePlayerItem occupied{};
        switch(rejection)
        {
        case 1:box.pev->flags=0;break;
        case 2:player.pev->flags|=FL_SPECTATOR;break;
        case 3:player.pev->health=0;break;
        case 4:player.m_bIsVIP=true;break;
        case 5:player.m_bShieldDrawn=true;break;
        case 6:pickup::restricted=true;break;
        case 7:player.m_rgpPlayerItems[PISTOL_SLOT]=&occupied;break;
        case 8:extendedAllowed=false;break;
        }
        pickup::removed=pickup::sounds=pickup::pairedCalls=0;
        g_engfuncs.pfnEmitSound=[](edict_t* entity,int channel,const char* sample,float volume,float attenuation,int flags,int pitch)
        {
            Check(entity==player.edict()&&channel==CHAN_ITEM&&!std::strcmp(sample,"items/gunpickup2.wav"));
            Check(volume==VOL_NORM&&attenuation==ATTN_NORM&&flags==0&&pitch==PITCH_NORM);++pickup::sounds;
        };
        const auto received=pickup::received;
        std::cout<<"Native Touch case "<<rejection<<'\n';box.Touch(&player);
        Check(pickup::pairedCalls==((rejection==0||rejection>=6)?1u:0u));
        if(rejection)
        {
            Check(box.m_rgpPlayerItems[PISTOL_SLOT]==&weapon&&weapon.m_pPlayer==nullptr);
            Check(!(box.pev->flags&FL_KILLME)&&pickup::removed==0&&pickup::sounds==0&&pickup::received==received);
            Check(weapon.m_iClip==75&&player.m_rgAmmo[1]==40);
            if(rejection==8)
            {
                Check(weapon.pev->owner==&boxEdict);
                extendedAllowed=true;box.Touch(&player);
                Check(box.m_rgpPlayerItems[PISTOL_SLOT]==nullptr&&weapon.m_pPlayer==&player);
                Check(pickup::removed==1&&pickup::sounds==1&&pickup::received==received+1);
                Check(weapon.m_iClip==75&&player.m_rgAmmo[1]==40);
                player.m_pActiveItem=&weapon;Check(weapon.Deploy());globals.time+=5;frame.pressed=1;
                weapon.PrimaryAttack();Check(weapon.m_iClip==74&&weapon.Status()==GlockStatus::Applied);
            }
        }
        else
        {
            Check(box.m_rgpPlayerItems[PISTOL_SLOT]==nullptr&&weapon.m_pPlayer==&player);
            Check((box.pev->flags&FL_KILLME)&&box.m_pfnTouch==nullptr&&pickup::removed==1&&pickup::sounds==1&&pickup::received==received+1);
            Check(weapon.m_iClip==75&&player.m_rgAmmo[1]==40&&weapon.pev->owner==player.edict());
            player.m_pActiveItem=&weapon;Check(weapon.Deploy());globals.time+=5;frame.pressed=1;
            weapon.PrimaryAttack();Check(weapon.m_iClip==74&&weapon.Status()==GlockStatus::Applied);
        }
    }
    pickup::restricted=false;extendedAllowed=true;player.m_bIsVIP=player.m_bShieldDrawn=false;
}
#endif

int main(int argc,char** argv)
{
    Check(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<char> input((std::istreambuf_iterator<char>(file)),{});
    Check(!input.empty());model.assign(reinterpret_cast<const std::byte*>(input.data()),
        reinterpret_cast<const std::byte*>(input.data()+input.size()));
    PickupRules rules;g_pGameRules=&rules;
    for(int clip:{0,1,14,15,28,75})
    {
        Initialize();g_pGameRules=&rules;globals.time=5;
        weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
        weapon.Precache();Check(weapon.Bind(player,prepared,backends)==GlockStatus::Ready);
        weapon.AttachToPlayer(&player);Check(weapon.Deploy());globals.time=10;
        weapon.m_iClip=clip;weapon.m_iDefaultAmmo=0;player.m_rgAmmo[1]=40;
        player.m_rgpPlayerItems[PISTOL_SLOT]=&weapon;
        // The actual DropPlayerItem calls this before CreateWeaponBox/Pack.
        weapon.BeforeWeaponDrop();
        edict_t boxEdict{};CWeaponBox box{};box.pev=&boxEdict.v;
        boxEdict.v.pContainingEntity=&boxEdict;boxEdict.pvPrivateData=&box;
        Check(!box.HasWeapon(&weapon));
        Check(box.PackWeapon(&weapon));
        Check(box.HasWeapon(&weapon));
        Check(box.m_rgpPlayerItems[PISTOL_SLOT]==&weapon);
        Check(player.m_rgpPlayerItems[PISTOL_SLOT]==nullptr&&player.m_pActiveItem==nullptr);
        Check(weapon.m_pPlayer==nullptr&&weapon.m_iClip==clip);
        Check(weapon.pev->owner==&boxEdict&&weapon.pev->solid==SOLID_NOT&&weapon.pev->modelindex==0);
        Check(!box.PackWeapon(&weapon));
        // This is the normal item branch called by the actual Touch loop. Full
        // Touch gates, external hooks and physical floor contact are not claimed.
        const auto before=pickup::received;
        Check(player.AddPlayerItem(&weapon));
        Check(pickup::received==before+1&&player.m_rgpPlayerItems[PISTOL_SLOT]==&weapon);
        Check(weapon.m_pPlayer==&player&&weapon.m_iClip==clip&&player.m_rgAmmo[1]==40);
        weapon.AttachToPlayer(&player);
        Check(weapon.pev->owner==player.edict()&&weapon.pev->aiment==player.edict());
        box.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;
        player.m_pActiveItem=&weapon;globals.time+=5;
        Check(weapon.Deploy());globals.time+=5;
        if(clip==0)
        {
            weapon.Reload();Check(weapon.m_fInReload);
            globals.time=weapon.ActionState().ownerAttack;
            weapon.ItemPostFrame();Check(weapon.m_iClip==15&&player.m_rgAmmo[1]==25);
            weapon.ItemPostFrame();globals.time+=5;
        }
        frame.pressed=1;events.clear();
        weapon.PrimaryAttack();Check(weapon.Status()==GlockStatus::Applied&&weapon.m_iClip==(clip?clip-1:14));
        Check(player.m_rgAmmo[1]==(clip?40:25));
        // Preserve the chosen cooperative effect branch after reacquiring the
        // item. Single-player server shell dispatch must not be added here.
        for(std::size_t at=0;at<events.size();)
        {
            Check(at+1<events.size());
            const auto kind=events[at],count=events[at+1];
            Check(kind!=static_cast<unsigned>(GlockEffect::Shell)&&count<=events.size()-at-2);
            at+=count+2;
        }
        globals.time+=5;Check(player.RemovePlayerItem(&weapon));
        Check(player.m_rgpPlayerItems[PISTOL_SLOT]==nullptr&&player.m_pActiveItem==nullptr);
    }
    Initialize();g_pGameRules=&rules;
    weapon.ConfigureColdHost({nullptr,Current,ColdPrepare,ColdOwner,ColdInventory});
    weapon.Precache();current=false;
    Check(!player.AddPlayerItem(&weapon));
    Check(player.m_rgpPlayerItems[PISTOL_SLOT]==nullptr);
#if COF_FULL_TOUCH
    FullTouchCases(rules);
    MixedBoxCase(rules);
#endif
    g_pGameRules=nullptr;
    std::cout<<"Glock actual inventory/weaponbox pack: "<<checks<<" checks; received="<<pickup::received
#if COF_FULL_TOUCH
             <<"; full native Touch executed; hooks/physics/multiplayer and live acceptance remain separate\n";
#else
             <<"; Touch/hooks/physics/multiplayer and live acceptance remain separate\n";
#endif
}
