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
    // 1 changed, 0 unchanged; -1 invalid, -2 unpaired, -3 unavailable, -4 exhausted.
    virtual int SetForm(int player,int form)=0;
    virtual void SetFormListener(FormListener listener)=0;
};
}
