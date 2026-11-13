// SPDX-License-Identifier: GPL-3.0-or-later
// The original lifecycle fixture supplies explicit engine/effect boundaries.
// Inventory insertion, removal and weapon-box packing run actual SDK bodies.
#include "shared-glock-sdk.inc"

namespace pickup {
unsigned received{},respawns{},switches{},resets{};
}
bool CSOL_IsMeleeReplacement(CBasePlayer&,CBasePlayerItem&);
void CSOL_RemoveBeamWeapon(CBasePlayerItem*);
#include "pickup-rules.inc"
#include "pickup-native.inc"

void CBasePlayer::ResetAutoaim(){Check(this==&player);}
void CBasePlayer::ResetMaxSpeed(){Check(this==&player);++pickup::resets;}
int CBasePlayer::SwitchWeapon(CBasePlayerItem*){std::abort();}
bool CSOL_IsMeleeReplacement(CBasePlayer& p,CBasePlayerItem& item)
{Check(&p==&player&&item.m_iId==896);return false;}
void CSOL_RemoveBeamWeapon(CBasePlayerItem* item){Check(item==&weapon);}
extern "C" int A_strcmp(const char* a,const char* b){return std::strcmp(a,b);}

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
    g_pGameRules=nullptr;
    std::cout<<"Glock actual inventory/weaponbox pack: "<<checks<<" checks; received="<<pickup::received
             <<"; original Touch/hook/physics/multiplayer and live acceptance remain separate\n";
}
