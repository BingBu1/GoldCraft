#pragma once
// In-process x86 interface, obtained from the loaded ReGameDLL CreateInterface.
// Include the HLSDK interface.h before this header. No pointer crosses IPC.
namespace goldcraft {
constexpr const char* server_control_api_version="GoldCraftServerControl001";
using FormListener=void(*)(int player,int previous,int current);
class IServerControl : public IBaseInterface {
public:
    virtual int GetForm(int player)=0; // -1 disconnected, 0 CS, 1 Minecraft
    virtual int IsPaired(int player)=0;
    // 1 changed, 0 unchanged; -1 invalid, -2 unpaired, -3 unavailable, -4 exhausted, -5 vetoed.
    virtual int SetForm(int player,int form)=0;
    virtual void SetFormListener(FormListener listener)=0;
};
// Optional extension. Keep ServerControl001's vtable and old Pawn natives intact.
constexpr const char* server_gameplay_api_version="GoldCraftServerGameplay001";
using FormGuard=int(*)(int player,int requested); // nonzero vetoes MC entry
using PlayerAttackListener=void(*)(int victim,int attacker,int kind,float health,float armor);
class IServerGameplay : public IBaseInterface {
public:
    // -2 unsupported bridge, -1 unpaired/unknown; otherwise connected=1,
    // creative=2, any worn armor=4, skill infection immunity=8 (150ms lease),
    // all from the authenticated MC server.
    virtual int GetPlayerFlags(int player)=0;
    // Positive only during the matching authenticated active player TakeDamage.
    virtual int GetPlayerAttack(int victim,int attacker)=0;
    virtual void SetFormGuard(FormGuard guard)=0;
    virtual void SetPlayerAttackListener(PlayerAttackListener listener)=0;
};
}
