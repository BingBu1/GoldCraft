// SPDX-License-Identifier: GPL-3.0-or-later
// Real common dispatcher/inventory and SKULL5 module; SDK boundary collectors.
#define main PreviousIntegrationMain
#include "../csol-skull-balrog/integration_client.cpp"
#undef main
#include "../../native/csol/client/weapons/skull5.hpp"
#include "../../native/csol/weapons/skull5.hpp"
#include "../../native/csol/client/weapons/weapon_module.hpp"
namespace scopefixture {
int scopeDraws{},resets{},metricCalls{},sprite{},color[3]{},width{},height{},gap{},drawStage{},crosshairRects{};
int requestedWidth=1280,requestedHeight=720,metric=15;
wrect_t rect{};std::vector<int> centers;
bool styleReady=true;float hudDraw=1;
}
namespace csol::client {
bool HudProportionalValue(int normalized,int& result){++scopefixture::metricCalls;Check(normalized==10,"original proportional input");result=scopefixture::metric;return scopefixture::styleReady;}
void DrawSniperScope(int w,int h,int g){Check(scopefixture::drawStage==1,"scope frame follows actual stock HUD consumer");++scopefixture::scopeDraws;scopefixture::width=w;scopefixture::height=h;scopefixture::gap=g;}
void ResetSniperScope(){++scopefixture::resets;}
float SharedRandomFloat(unsigned,float low,float high){return (low+high)*.5f;}
float SharedSpread(unsigned){return 0;}
int ClientCRTRandom(){return 0;}
}
namespace {
void SetFovMessage(int fov){Send("SetFOV",{std::byte(fov)});}
void Draw(){scopefixture::drawStage=0;WeaponRedraw(now,0);}
void ScopeTests(){using namespace scopefixture;
 Setup();
 engine.pfnGetCvarFloat=[](const char* name){return !std::strcmp(name,"cl_lw")?localPrediction:!std::strcmp(name,"hud_draw")?hudDraw:!std::strcmp(name,"cl_crosshair_translucent")?0.f:1.f;};
 engine.pfnFillRGBABlend=[](int,int,int,int,int,int,int,int){++crosshairRects;};
 engine.pfnSPR_Load=[](const char* name){return std::strcmp(name,"sprites/csol/sniper_scope.spr")==0?42:1;};
 engine.pfnSetCrosshair=[](int s,wrect_t r,int red,int green,int blue){sprite=s;rect=r;color[0]=red;color[1]=green;color[2]=blue;centers.push_back(s);};
 engine.pfnGetScreenInfo=[](SCREENINFO* s){s->iWidth=requestedWidth;s->iHeight=requestedHeight;return 1;};
 downstream.HUD_Redraw=[](float,int){drawStage=1;return 37;};
 Send(WeaponDefinitionMessage,EncodeWeaponDefinition({112,0,1,24,-1,240,-1,24,0,skull5::Classname}));
 Select(112,24,240);SetFovMessage(90);Draw();Check(scopeDraws==0&&sprite==0,"unzoomed has no scope");Check(crosshairRects==0,"actual shared consumer suppresses SKULL5 dynamic crosshair");
 Select(m249ex::Id,120,240);Draw();Check(crosshairRects>0,"ordinary dynamic crosshair policy of other gun remains active");Select(112,24,240);
 for(int fov:{80,35})for(int scale:{0,10,15,22,30}){
  SetFovMessage(fov);metric=scale;const int before=scopeDraws;Draw();
  Check(scopeDraws==before+1&&width==1280&&height==720&&gap==scale,"both FOVs use actual scheme metric");
  Check(sprite==42&&rect.left==0&&rect.top==0&&rect.right==256&&rect.bottom==256&&color[0]==255&&color[1]==255&&color[2]==255,"native center sprite exact original rect and white");
 }
 SetFovMessage(90);Draw();Check(sprite==0,"unzoom clears owned sprite");
 SetFovMessage(35);Draw();int before=scopeDraws;view.health=0;Draw();Check(scopeDraws==before&&sprite==0,"death hides scope");view.health=100;Draw();Check(sprite==42,"alive scope resumes");
 view.spectator=1;Draw();Check(sprite==0,"spectator hides scope");view.spectator=0;Draw();
 Send("HideWeapon",{std::byte{64}});Draw();Check(sprite==0,"hide crosshair flag clears scope");Send("HideWeapon",{std::byte{0}});Draw();
 hudDraw=0;Draw();Check(sprite==0,"hud_draw zero clears scope");hudDraw=1;Draw();
 // Stock CurWeapon can precede the extended ownership message. Its actual
 // original consumer sets the new gun's center before the next HUD redraw.
 Send("CurWeapon",{std::byte{1},std::byte{3},std::byte{10}});
 engine.pfnSetCrosshair(77,{0,16,0,16},255,255,255);Draw();
 Check(sprite==77,"stock zoom sprite survives pending extended ownership message");
 Select(112,24,240);SetFovMessage(35);Draw();Send("CurWeapon",{std::byte{0},std::byte{3},std::byte{10}});Check(sprite==42,"inactive stock update cannot clear current scope");
 Select(29,-1,0);engine.pfnSetCrosshair(77,{0,16,0,16},255,255,255);Send("CurWeapon",{std::byte{1},std::byte{29},std::byte{255}});Draw();Check(sprite==77,"extended message before stock message preserves new center");
 Select(29,-1,0);Draw();Select(112,24,240);SetFovMessage(35);Draw();
 ResetWeapons(false);Check(sprite==0,"round reset clears owned center");
 Select(112,24,240);SetFovMessage(35);Draw();WeaponVideoInit();Check(sprite==0&&resets>0,"video reset clears sprite and GL owner");
 Select(112,24,240);SetFovMessage(35);Draw();const auto cleanup=resets;ShutdownWeapons();Check(sprite==0&&resets==cleanup+1,"shutdown releases both center and graphics");ShutdownWeapons();Check(sprite==0,"repeat shutdown remains harmless");
 Check(!FindWeaponModule(112)->dynamicCrosshair,"SKULL5 no ordinary dynamic crosshair by original flags");
}
}
int main(){ScopeTests();std::cout<<"scope actual client "<<checks<<" checks "<<failures<<" failures\n";return failures?1:0;}
