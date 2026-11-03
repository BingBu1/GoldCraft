// SPDX-License-Identifier: GPL-3.0-or-later
// Public message/inventory/prediction consumers against actual SDK TUs.
#define wmain PreviousSkull1Checks
#include "skull1_integration_client.cpp"
#undef wmain
#include "../../native/csol/client/skull5.hpp"
#include "../../native/csol/skull5.hpp"
namespace {
void LoadSkull5Lists(){
 const auto key=WeaponHudList(skull5::Classname);std::istringstream in{std::string(GeneratedWeaponResource(key))};int count{};in>>count;
 Check(count==7&&key=="sprites/csol/weapon_skull5_v1.txt","public descriptor uses original seven SKULL5 rows");
 for(int i=0;i<count;++i){std::string name,atlas;int resolution,x,y,w,h;in>>name>>resolution>>atlas>>x>>y>>w>>h;Check(bool(in),"descriptor row parses");
  client_sprite_t row{};std::strcpy(row.szName,name.c_str());std::strcpy(row.szSprite,atlas.c_str());row.iRes=resolution;row.rc={x,x+w,y,y+h};lists[key].push_back(row);}
 auto stale=lists[key];for(auto& row:stale)std::strcpy(row.szSprite,"stale/skull5");lists["sprites/weapon_skull5.txt"]=stale;
}
void Skull5Checks(){
 // Model the host's non-weapon state propagation between consecutive commands.
 // The older shared fixture leaves `to.health` zero on the ID0 stock early-out.
 downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t*,int,double,unsigned){
  ++stockCalls;Check(from->client.m_iId<32,"stock table never receives extended ID");*to=*from;};
 Send(WeaponDefinitionMessage,EncodeWeaponDefinition({skull5::Id,0,1,24,-1,240,-1,24,0,skull5::Classname}));
 WeaponOwnership owned{};owned[112]=owned[29]=true;
 for(const auto& packet:EncodeWeaponInventory(owned,3))Send(WeaponInventoryMessage,packet);
 Check(events.contains(skull5::Event),"public initialization hooks SKULL5 fire event");
 Select(112,24,240);auto hud=CurrentWeaponHudState();
 Check(hud.id==112&&hud.slot==0&&hud.kind==hud_style::AmmoKind::Magazine&&!hud.specialAmmo&&hud.clip==24&&hud.reserve==240,
       "SKULL5 public HUD is primary ordinary magazine, never special total ammo");
 LoadLists();LoadSkull5Lists();WeaponVideoInit();Select(112,24,240);WeaponRedraw(now,0);
 Check(std::find(requestedSprites.begin(),requestedSprites.end(),"sprites/csol/640hud7_v2.spr")!=requestedSprites.end(),"SKULL5 uses corrected original ammo atlas");
 Check(crosshairParts>0,"ordinary unzoomed SKULL5 crosshair drawn");
 commands.at("slot1")();usercmd_t selection{};selection.buttons=IN_ATTACK;sent.clear();WeaponInventoryCreateMove(.01f,&selection,1);
 Check(!(selection.buttons&IN_ATTACK)&&sent=="weapon_skull5","primary slot confirms exact selected SKULL5 and consumes attack");
 requestedLists.clear();draws.clear();ResetPickupHistory();AddWeaponPickup(112);DrawPickupHistory(0,0);
 Check(requestedLists==std::vector<std::string>{"sprites/csol/weapon_skull5_v1.txt"}&&draws.size()==1&&draws[0].path=="sprites/csol/640hud57.spr",
       "public pickup renders original SKULL5 weapon card");
 requestedLists.clear();draws.clear();ResetPickupHistory();Send(AmmoPickupMessage,EncodeAmmoPickup({112,0,30}));DrawPickupHistory(0,0);
 Check(requestedLists==std::vector<std::string>{"sprites/csol/weapon_skull5_v1.txt"}&&draws.size()==1&&draws[0].path=="sprites/csol/640hud7_v2.spr"&&
       draws[0].rectangle.left==230&&draws[0].rectangle.right==254&&draws[0].rectangle.top==96&&draws[0].rectangle.bottom==120,
       "public ammo pickup uses original 24x24 SKULL5 ammo rectangle");
 auto state=State(112,24,240);state.client.weaponanim=skull5::Draw;state.weapondata[0].fuser1=1.f;
 auto result=Dispatch(state,IN_ATTACK);hud=CurrentWeaponHudState();
 Check(result.weapondata[0].m_iClip==23&&hud.clip==23&&hud.reserve==240,"public prediction consumes exactly one loaded round");
 const int previousAnimations=animCount;auto cooling=Dispatch(result,IN_ATTACK);
 Check(cooling.client.health==100&&cooling.weapondata[0].m_iClip==23&&animCount==previousAnimations,"live SKULL5 cooldown cannot replay fire or consume another round");
 Select(29,-1,0);const int before=animCount;Dispatch(state,IN_ATTACK);event_args_t args{};args.entindex=1;events.at(skull5::Event)(&args);
 Check(animCount==before,"late SKULL5 prediction/event cannot animate selected knife");
 Select(112,17,210);hud=CurrentWeaponHudState();Check(hud.clip==17&&hud.reserve==210,"reliable switch-back replaces old prediction");
 localPrediction=0;Dispatch(state,IN_ATTACK);Check(CurrentWeaponHudState().clip==17,"cl_lw0 retains server clip in public HUD");localPrediction=1;
 Select(112,24,240);auto zoom=State(112,24,240);zoom.weapondata[0].fuser1=1.f;
 auto zoomed=Dispatch(zoom,IN_ATTACK2);Check(zoomed.client.fov==80,"first public secondary uses original 80 FOV");
 zoomed.weapondata[0].m_flNextSecondaryAttack=-.001f;zoomed.client.m_flNextAttack=-.001f;
 auto zoomed2=Dispatch(zoomed,IN_ATTACK2);Check(zoomed2.client.fov==35,"second public secondary uses original 35 FOV");
 InitializeDeathNotices();char path[]="sprites/hud.txt";int count{};auto* icons=engine.pfnSPR_GetList(path,&count);
 for(int resolution:{320,640}){auto* icon=std::find_if(icons,icons+count,[&](const auto& r){return r.iRes==resolution&&!std::strcmp(r.szName,"d_skull5");});
  Check(icon!=icons+count&&!std::strcmp(icon->szSprite,"csol/640hud4")&&icon->rc.left==172&&icon->rc.top==144&&icon->rc.right==220&&icon->rc.bottom==160,
        "both death HUD resolutions retain original SKULL5 rectangle");}
 Check(std::none_of(requestedSprites.begin(),requestedSprites.end(),[](const auto& s){return s.find("stale")!=std::string::npos;}),"old weapon descriptor cache cannot leak into draws");
 ResetWeapons(false);Check(CurrentWeaponHudState().id==0&&Skull5PredictedAmmo()==-1&&Skull5PredictedReserve()==-1,"round reset clears SKULL5 prediction and ownership display");
 Check(FindWeaponDefinition(112),"round reset preserves registered SKULL5 definition");
 level="maps/de_dust2.bsp";SynchronizeFrameSession();Check(!FindWeaponDefinition(112)&&Skull5PredictedAmmo()==-1,"map change clears registration/prediction");
 Check(pushes==pops&&nativeResolves==1,"prediction trace stacks balanced and native RNG entry cached");
}
}
int wmain(int argc,wchar_t** argv){
 try{if(argc!=2)return 2;const auto path=std::filesystem::path(argv[1]).string();native_random_test::Image original(path.c_str(),native_fixture::spans);
  Setup();nativeModule=original.Base();nativeFunction=original.Address<void*>(native_fixture::functionRva);
  api->GetClientBase=[](){return nativeModule;};api->ResolveGameSymbol=[](void* module,const char* name,mh_gamesymbol_kind_t kind,void** output){++nativeResolves;Check(module==nativeModule&&!std::strcmp(name,"UTIL_SharedRandomFloat")&&kind==MH_GAMESYMBOL_KIND_FUNCTION,"exact host RNG ABI");*output=nativeFunction;return MH_GAMESYMBOL_OK;};
  InitializeSharedRandom();Skull5Checks();ShutdownSharedRandom();std::cout<<"SKULL5 public client "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
