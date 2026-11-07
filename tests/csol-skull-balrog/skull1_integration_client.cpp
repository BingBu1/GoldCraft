// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the public SDK/message/HUD consumers with a real host RNG entry.
#define main PreviousClientChecks
#include "integration_client.cpp"
#undef main
#include "../../native/csol/client/weapons/skull1.hpp"
#include "../../native/csol/client/shared_random.hpp"
#include "../../native/csol/weapons/skull1.hpp"
#include "../../native/csol/weapons/weapon_resources.hpp"
#include "../../native/csol/weapons/weapon_hud_paths.hpp"
#include "../../native/csol/client/pickup_history.hpp"
#include "../csol-shared-random/native_image.hpp"
#include <filesystem>
#include <sstream>
namespace native_fixture {
#include "native_spans.inc"
}
namespace {
std::map<std::string,std::vector<client_sprite_t>> lists;
std::vector<std::string> requestedLists,requestedSprites;
std::map<int,std::string> spriteNames;
struct Draw{std::string path;wrect_t rectangle;};std::vector<Draw> draws;
int currentSprite{},crosshairParts{},nativeResolves{};void* nativeModule{};void* nativeFunction{};
void LoadLists(){
 const auto key=WeaponHudList(skull1::Classname);std::istringstream in{std::string(GeneratedWeaponResource(key))};int count{};in>>count;
 Check(count==3&&key=="sprites/csol/weapon_skull1_v1.txt","SKULL1 public descriptor routes to generated version");
 for(int i=0;i<count;++i){std::string name,atlas;int resolution,x,y,w,h;in>>name>>resolution>>atlas>>x>>y>>w>>h;Check(bool(in),"descriptor row parses");
  client_sprite_t row{};std::strcpy(row.szName,name.c_str());std::strcpy(row.szSprite,atlas.c_str());row.iRes=resolution;row.rc={x,x+w,y,y+h};lists[key].push_back(row);}
 auto stale=lists[key];for(auto& row:stale)std::strcpy(row.szSprite,"stale/640hud64");lists["sprites/weapon_skull1.txt"]=stale;
 engine.pfnSPR_GetList=[](char* path,int* count)->client_sprite_t*{
  requestedLists.emplace_back(path);auto found=lists.find(path);if(found==lists.end()){static client_sprite_t ordinary{};ordinary.iRes=640;std::strcpy(ordinary.szName,"ordinary");*count=1;return &ordinary;}
  *count=int(found->second.size());return found->second.data();};
 engine.pfnSPR_Load=[](const char* path){requestedSprites.emplace_back(path);int id=int(spriteNames.size())+1;spriteNames[id]=path;return id;};
 engine.pfnSPR_Set=[](int sprite,int,int,int){currentSprite=sprite;};
 engine.pfnSPR_DrawAdditive=[](int,int,int,const wrect_t* r){draws.push_back({spriteNames[currentSprite],*r});};
 engine.pfnFillRGBA=[](int,int,int,int,int,int,int,int){++crosshairParts;};
 engine.pfnFillRGBABlend=[](int,int,int,int,int,int,int,int){++crosshairParts;};
 engine.pfnGetCvarFloat=[](const char* name){
  return !std::strcmp(name,"cl_lw")?localPrediction:
   (!std::strcmp(name,"hud_fastswitch")||!std::strcmp(name,"cl_crosshair_translucent"))?0.f:1.f;};
 engine.pfnDrawStringReverse=[](int,int,const char*,int,int,int){return 0;};engine.pfnDrawString=[](int,int,const char*,int,int,int){return 0;};
 downstream.HUD_Redraw=[](float,int){return 1;};
}
void Skull1Checks(){
 Send(WeaponDefinitionMessage,EncodeWeaponDefinition({skull1::Id,1,1,32,-1,35,-1,7,0,skull1::Classname}));
 WeaponOwnership owned{};owned[130]=owned[96]=owned[203]=owned[29]=true;
 for(const auto& packet:EncodeWeaponInventory(owned,2))Send(WeaponInventoryMessage,packet);
 Check(events.contains(skull1::Event)&&events.contains(skull1::RapidEvent),"public initialization registers both SKULL1 events");
 Select(130,7,35);auto hud=CurrentWeaponHudState();
 Check(hud.id==130&&hud.slot==1&&hud.kind==hud_style::AmmoKind::Magazine&&!hud.specialAmmo&&hud.clip==7&&hud.reserve==35,"public reliable HUD selects secondary ordinary magazine and full reserve");
 commands.at("slot2")();usercmd_t selection{};selection.buttons=IN_ATTACK;sent.clear();WeaponInventoryCreateMove(.01f,&selection,1);
 Check(!(selection.buttons&IN_ATTACK)&&sent=="weapon_skull1","secondary slot confirms exact weapon command and consumes attack");
 commands.at("slot1")();selection.buttons=IN_ATTACK;sent.clear();WeaponInventoryCreateMove(.01f,&selection,1);
 Check(!(selection.buttons&IN_ATTACK)&&!sent.empty()&&sent!="weapon_skull1","primary slot remains separate from SKULL1");
 LoadLists();WeaponVideoInit();Select(130,7,35);WeaponRedraw(now,0);
 Check(std::find(requestedSprites.begin(),requestedSprites.end(),"sprites/csol/640hud2.spr")!=requestedSprites.end(),"public ammo icon loads original SKULL1 atlas");
 Check(crosshairParts>0,"public SKULL1 crosshair draw is not suppressed");
 requestedLists.clear();commands.at("slot2")();DrawWeaponSelection(0,0);commands.at("cancelselect")();
 Check(std::find(requestedLists.begin(),requestedLists.end(),"sprites/csol/weapon_skull1_v1.txt")!=requestedLists.end(),"selection reads generated versioned SKULL1 descriptor");
 requestedLists.clear();draws.clear();ResetPickupHistory();AddWeaponPickup(130);DrawPickupHistory(0,0);
 Check(requestedLists==std::vector<std::string>{"sprites/csol/weapon_skull1_v1.txt"}&&draws.size()==1&&draws[0].path=="sprites/csol/640hud64.spr","weapon pickup renders original weapon card through public consumer");
 requestedLists.clear();draws.clear();ResetPickupHistory();Send(AmmoPickupMessage,EncodeAmmoPickup({130,0,7}));DrawPickupHistory(0,0);
 Check(requestedLists==std::vector<std::string>{"sprites/csol/weapon_skull1_v1.txt"}&&draws.size()==1&&draws[0].path=="sprites/csol/640hud2.spr"&&draws[0].rectangle.left==25&&draws[0].rectangle.top==231&&draws[0].rectangle.right==49&&draws[0].rectangle.bottom==255,"ammo pickup uses original 24x24 SKULL1 rectangle");
 Check(std::none_of(requestedSprites.begin(),requestedSprites.end(),[](const auto& s){return s.find("stale")!=std::string::npos;}),"available old descriptor cache never leaks to SKULL1 draws");
 auto state=State(130,7,35);state.client.weaponanim=skull1::Draw;state.weapondata[0].fuser1=.95f;
 auto result=Dispatch(state,IN_ATTACK);hud=CurrentWeaponHudState();
 Check(result.weapondata[0].m_iClip==6&&hud.clip==6&&hud.reserve==35,"public dispatcher reaches SKULL1 and publishes its predicted count");
 const int previousAnimations=animCount;Dispatch(result,IN_ATTACK);Check(animCount==previousAnimations,"public dispatcher does not replay cooling-down SKULL1 animation");
 Select(29,-1,0);const int before=animCount;Dispatch(state,IN_ATTACK);
 for(const char* event:{skull1::Event,skull1::RapidEvent}){event_args_t args{};args.entindex=1;events.at(event)(&args);}
 Check(animCount==before,"late SKULL1 prediction and both events cannot animate selected knife");
 Select(130,4,21);hud=CurrentWeaponHudState();Check(hud.clip==4&&hud.reserve==21,"switch-back reliable state replaces old predicted HUD");
 localPrediction=0;Select(130,7,35);Dispatch(state,IN_ATTACK);Check(CurrentWeaponHudState().clip==7,"disabled prediction preserves reliable ammo in public HUD");localPrediction=1;
 InitializeDeathNotices();char path[]="sprites/hud.txt";int count{};auto* icons=engine.pfnSPR_GetList(path,&count);
 for(int resolution:{320,640}){auto* icon=std::find_if(icons,icons+count,[&](const auto& r){return r.iRes==resolution&&!std::strcmp(r.szName,"d_skull1");});
  Check(icon!=icons+count&&!std::strcmp(icon->szSprite,"csol/640hud4")&&icon->rc.left==220&&icon->rc.top==48&&icon->rc.right==252&&icon->rc.bottom==64,"both HUD resolutions use original 32x16 death rectangle");}
 Check(engine.pfnSPR_GetList(path,&count)==icons,"augmented death icon array retains stable pointer");
 ResetWeapons(false);Check(CurrentWeaponHudState().id==0&&Skull1PredictedAmmo()==-1&&Skull1PredictedReserve()==-1,"public round reset clears SKULL1 prediction owners");Check(FindWeaponDefinition(130),"round reset retains SKULL1 registration");
 level="maps/de_dust2.bsp";SynchronizeFrameSession();Check(!FindWeaponDefinition(130)&&Skull1PredictedAmmo()==-1,"map reset clears SKULL1 registration and cached state");
 Check(pushes==pops&&nativeResolves==1,"public predictions balance trace stack and reuse initialized native entry");
}
}
int wmain(int argc,wchar_t** argv){
 try{if(argc!=2)return 2;const auto path=std::filesystem::path(argv[1]).string();native_random_test::Image original(path.c_str(),native_fixture::spans);
  Setup();nativeModule=original.Base();nativeFunction=original.Address<void*>(native_fixture::functionRva);
  api->GetClientBase=[](){return nativeModule;};api->ResolveGameSymbol=[](void* module,const char* name,mh_gamesymbol_kind_t kind,void** output){++nativeResolves;Check(module==nativeModule&&!std::strcmp(name,"UTIL_SharedRandomFloat")&&kind==MH_GAMESYMBOL_KIND_FUNCTION,"exact native resolver contract");*output=nativeFunction;return MH_GAMESYMBOL_OK;};
  InitializeSharedRandom();Skull1Checks();ShutdownSharedRandom();std::cout<<"SKULL1 public client "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
