// SPDX-License-Identifier: GPL-3.0-or-later
#define main OriginalHudRegression
#include "tests/csol-hud-style/client.cpp"
#undef main
#include "native/csol/client/weapons/weapon_module.hpp"
#include "native/guest/cof/client/revolver_hud.hpp"
#include <limits>

// Crosshair is independently verified; these functions are never invoked here.
namespace csol::client
{
void InitializeGuestCofCrosshair() { std::abort(); }
void ResetGuestCofCrosshair() noexcept { std::abort(); }
void GuestCofCrosshairVideoInit() noexcept { std::abort(); }
void ShutdownGuestCofCrosshair() noexcept { std::abort(); }
void DrawGuestCofCrosshair(int,int,bool) noexcept { std::abort(); }
}

int main()
{
    using namespace csol::client;using namespace fixture;
    char executable[]="revolver-hud";char* arguments[]{executable,nullptr};
    if(OriginalHudRegression(1,arguments))return 1;
    Configure();SetupResources();Seed();
    constexpr const char* prefix="resource/csol/hud/v1/revolver";
    const auto* module=FindWeaponModule(898);
    Check(module&&module->hudState&&module->ammoKind==csol::hud_style::AmmoKind::Magazine,
        "real statically registered leftwheel module supplies typed frame producer");
    Check(FindWeaponModule(896)&&FindWeaponModule(897),"previous guest modules remain registered");
    const std::string base=prefix;
    std::string frames;
    for(int i=0;i<6;++i)
    {
        if(i)frames+=',';
        frames+='"'+base+std::to_string(i)+".tga\"";
        files[base+std::to_string(i)+".tga"]=Tga(32,32);
    }
    const std::string row="{\"name\":\"weapon_cof_revolver\",\"title\":\"客串武器·Taurus .357\","
        "\"weapon\":\""+std::string(model)+"\",\"selected\":\""+model+"\","
        "\"ammo\":\""+base+"0.tga\",\"ammo2\":\"\",\"ammoFrames\":["+frames+"]}";
    Put(csol::hud_style::CatalogPath,"{\"version\":1,\"weapons\":["+row+"],\"portraits\":[]}");
    definition.id=898;definition.name="weapon_cof_revolver";definition.slot=1;
    state={};state.id=898;state.slot=1;state.kind=csol::hud_style::AmmoKind::Magazine;
    state.reserve=500;Prepare();PrepareSelection();
    Check(HudStyleWeaponResourcesReady(898,6),"actual cold HUD loads all six frames before readiness");
    const auto stableReads=reads;
    const auto handles=selectionCards.at(898).ammoFrames;
    for(int clip:{0,1,2,3,4,5,6,75,511,65535,std::numeric_limits<int>::max()})
    {
        state.clip=clip;module->hudState(state);Draw();
        const auto index=guest::cof::client::RevolverAmmoFrame(clip);
        Check(state.ammoFrame==index&&ammoImage.id==handles[index].id,"clip selects proved original frame with explicit high-ammo projection");
        Check(WeaponDigits()==std::to_string(clip)+"500","authoritative clip and reserve remain complete numbers");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==handles[index].id&&q.additive&&q.right-q.x==32&&q.bottom-q.y==32;}),
            "selected frame uses existing additive32px HUD icon placement");
    }
    Check(reads==stableReads,"frame selection performs no resource reads");
    capture.record=false;allocation_probe::calls=0;allocation_probe::active=true;
    for(int i=0;i<100;++i)
    {state.clip=i%6;module->hudState(state);BeginHudStyle(0,0);DrawHudStyle();Paint();}
    allocation_probe::active=false;capture.record=true;
    Check(allocation_probe::calls==0&&reads==stableReads,"100 actual hot redraws allocate and read zero resources");
    state.ammoFrame=6;Draw();Check(!HudStyleWeaponVisible(),"invalid explicit frame never borrows a static or other frame");
    for(int missing=0;missing<6;++missing)
    {
        const auto path=base+std::to_string(missing)+".tga";
        auto saved=files.at(path);InvalidateSelectedRow();const auto live=capture.live.size();files.erase(path);
        state.clip=missing;module->hudState(state);PrepareSelection();Draw();
        Check(!HudStyleWeaponResourcesReady(898,6)&&!HudStyleWeaponVisible()&&capture.live.size()==live,
            "missing independent frame rejects entire staged row without leaking textures");
        const auto failedReads=reads;PrepareSelection();Check(reads==failedReads,"missing frame uses bounded existing cold retry");
        files[path]=Tga(31,32);selectionCards.at(898).imageRetryAfter=0;PrepareSelection();
        Check(!HudStyleWeaponResourcesReady(898,6)&&capture.live.size()==live,"wrong frame size rejects atomic row");
        files[path]=std::move(saved);selectionCards.at(898).imageRetryAfter=0;PrepareSelection();Draw();
        Check(HudStyleWeaponResourcesReady(898,6)&&HudStyleWeaponVisible(),"verified frame recovery prepares full row");
    }
    HudStyleVideoInit();Check(capture.live.empty(),"video replacement deletes all frame textures exactly once");
    extension=nullptr;privateFont=nullptr;surface=nullptr;schemes=nullptr;
    std::cout<<"Revolver actual HUD checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
