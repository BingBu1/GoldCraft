// SPDX-License-Identifier: GPL-3.0-or-later
// Complete HUD preparation/paint with controlled inventory-title and VGUI APIs.
#define main PreviousHudChecks
#include "../csol-hud-style/client.cpp"
#undef main
int main()
{
 using namespace fixture;using namespace csol::client;
 Configure();SetupResources();Prepare();Seed();
 const char* aCard="resource/csol/hud/v1/skull3.tga";
 const char* dCard="resource/csol/hud/v1/skull3d.tga";
 const char* aAmmo="resource/csol/hud/v1/skull3_ammo.tga";
 const char* dAmmo="resource/csol/hud/v1/skull3d_ammo.tga";
 files[aCard]=Tga(170,45);files[dCard]=Tga(170,45);files[aAmmo]=Tga(24,24);files[dAmmo]=Tga(24,24);
 catalog.weapons["weapon_skull3"]={"噬魂 SKULL-3",aCard,aAmmo,{}};
 catalog.weapons["weapon_skull3d"]={"Own D provider title",dCard,dAmmo,{}};
 csol::WeaponDefinition paired;paired.id=142;paired.slot=0;paired.name="weapon_skull3";
 definition.id=143;definition.slot=0;definition.name="weapon_skull3d";
 state={};state.id=143;state.slot=0;state.clip=70;state.reserve=200;state.kind=csol::hud_style::AmmoKind::Magazine;
 titleDefinition=&paired;Draw();
 Check(weaponTitle==L"1 噬魂 SKULL-3","resolved inventory title includes original slot prefix");
 Check(weaponName=="weapon_skull3d"&&weapon.id==143,"title fallback does not change actual HUD weapon");
 const auto card=weaponImage.id,icon=ammoImage.id;const auto before=reads;
 Check(card&&icon&&HudStyleWeaponVisible(),"D keeps complete card and ammo row");
 titleDefinition=nullptr;Draw();
 Check(weaponTitle==L"1 Own D provider title","losing A while D stays selected restores D own title");
 Check(weaponImage.id==card&&ammoImage.id==icon&&reads==before,"title-only change never reloads or substitutes D textures");
 titleDefinition=&paired;Draw();Check(weaponTitle==L"1 噬魂 SKULL-3","ownership restoration refreshes same-id cached title");
 Check(weaponImage.id==card&&ammoImage.id==icon&&reads==before,"restoration keeps D textures");
 Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& text){return text.value==L"1 噬魂 SKULL-3";}),"resolved title reaches real paint");
 paired.slot=1;Draw();Check(weaponTitle==L"2 噬魂 SKULL-3","changed source slot invalidates prefix cache");
 titleDefinition=nullptr;definition.slot=2;Draw();Check(weaponTitle==L"3 Own D provider title","own title slot changes do not use stale prefix");
 const auto stable=reads;Draw();Check(reads==stable&&weaponImage.id==card,"stable redraw performs no resource reads");
 HudStyleVideoInit();Check(weaponTitleId==0&&weaponTitleSlot==-1&&weaponTitle.empty(),"video reset clears title cache");
 privateFont=nullptr;extension=nullptr;ShutdownHudStyle();
 std::cout<<"title_hud checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
