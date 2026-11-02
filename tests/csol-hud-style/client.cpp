// SPDX-License-Identifier: GPL-3.0-or-later
// Entire production consumer and real TGA codec. Engine, surface and font
// handles are controlled boundaries; this is not a GL/font pixel test.
#include "native/csol/client/hud_style.cpp"
#include "tests/csol-hud-style/surface_stub.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <source_location>

namespace fixture {
unsigned checks{},failures{},reads{},forwards{},syncs{};
unsigned installs{};
metahook_api_t apiData{};
std::map<std::string,usermsg_t> messages;
bool newSession{};
float hudDraw=1;
int scenarioLevel=121;
std::map<std::string,std::vector<std::byte>> files;
csol::WeaponDefinition definition{};
csol::hud_style::WeaponState state{};
csol::Character character{};
bool haveCharacter=true;
void Check(bool value,const char* text,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<text<<'\n';}}
void Log(const char*,...){}
int Previous(const char*,int,void*){++forwards;return 73;}
int NativeReplacement(const char*,int,void*){++forwards;return 91;}
int healthGlobal=1900,armorGlobal=400;
std::vector<std::byte> Bytes(std::uint32_t n,unsigned length)
{std::vector<std::byte> v;for(unsigned i=0;i<length;++i)v.push_back(std::byte(n>>(i*8)));return v;}
std::vector<std::byte> Tga(unsigned w,unsigned h)
{
    std::vector<std::byte> b(18+w*h*4,std::byte{0xff});std::fill_n(b.begin(),18,std::byte{});
    b[2]=std::byte{2};b[12]=std::byte(w);b[13]=std::byte(w>>8);b[14]=std::byte(h);b[15]=std::byte(h>>8);
    b[16]=std::byte{32};b[17]=std::byte{0x28};return b;
}
void Put(const std::string& path,const std::string& text)
{auto& b=files[path];b.resize(text.size());std::memcpy(b.data(),text.data(),text.size());}
const char* model="resource/csol/hud/v1/cannon.tga";
const char* ammo="resource/csol/hud/v1/ammo.tga";
const char* ammo2="resource/csol/hud/v1/ammo2.tga";
const char* face="resource/csol/hud/v1/p1.tga";
const std::string json=R"({"version":1,"weapons":[{"name":"weapon_cannon","title":"黑龙炮","weapon":"resource/csol/hud/v1/cannon.tga","ammo":"resource/csol/hud/v1/ammo.tga","ammo2":"resource/csol/hud/v1/ammo2.tga"}],"portraits":[{"id":1,"path":"resource/csol/hud/v1/p1.tga"}]})";
struct Quad {int texture{},x{},y{},right{},bottom{};float u0{},u1{};std::array<int,4> color{};bool additive{};};
struct Text {std::wstring value;int x{},y{};};
struct Filled {int x{},y{},right{},bottom{};std::array<int,4> color{};};
struct Surface final:vgui::HudTestSurface
{
    int width=1280,height=720,current{},next=10,pushes{},pops{},root=1,x{},y{};
    std::array<int,4> color{};
    std::set<int> live;
    std::map<int,csol::Image> uploads;
    std::vector<Quad> quads;
    std::vector<Text> texts;
    std::vector<Filled> fills;
    vgui::VPANEL GetEmbeddedPanel()override{return root;}
    void GetScreenSize(int& w,int& h)override{w=width;h=height;}
    void PushMakeCurrent(vgui::VPANEL,bool)override{++pushes;}
    void PopMakeCurrent(vgui::VPANEL)override{++pops;}
    int CreateNewTextureID(bool)override{live.insert(++next);return next;}
    bool DeleteTextureByID(int id)override{Check(live.erase(id)==1,"texture deleted exactly once");uploads.erase(id);return true;}
    void DrawSetTextureRGBA(int id,const unsigned char* rgba,int w,int h,int,bool)override
    {Check(live.contains(id)&&rgba&&w>0&&h>0,"real TGA bytes uploaded to an owned texture");uploads[id]={unsigned(w),unsigned(h),{rgba,rgba+w*h*4}};}
    void DrawSetTexture(int id)override{current=id;}
    void DrawSetColor(int r,int g,int b,int a)override{color={r,g,b,a};}
    void DrawFilledRect(int x0,int y0,int x1,int y1)override{fills.push_back({x0,y0,x1,y1,color});}
    void DrawTexturedPolygon(vgui::VGuiVertex* v,int count)override
    {Check(count==4,"HUD uses four vertices");quads.push_back({current,v[0].GetX(),v[0].GetY(),v[2].GetX(),v[2].GetY(),v[0].GetU(),v[2].GetU(),color});}
    void DrawTexturedRectAdd(int x0,int y0,int x1,int y1)override
    {quads.push_back({current,x0,y0,x1,y1,0,1,color,true});}
    void GetTextSize(vgui::HFont,const wchar_t* t,int& w,int& h)override{w=int(std::wcslen(t))*9;h=13;}
    void DrawSetTextPos(int a,int b)override{x=a;y=b;}
    void DrawPrintText(const wchar_t* t,int n)override{texts.push_back({std::wstring(t,n),x,y});}
} capture;
cvar_t option{},scale{};
void SetupResources()
{
    files.clear();Put(csol::hud_style::CatalogPath,json);
    for(const auto& r:csol::hud_style::Resources)files[r.path]=Tga(r.width,r.height);
    auto& atlas=files[csol::hud_style::Resources[unsigned(csol::hud_style::Art::WhiteDigits)].path];
    for(unsigned y=0;y<22;++y)for(unsigned x=0;x<170;++x)
    {const auto at=18+(y*170+x)*4;atlas[at]=std::byte(x/17+1);atlas[at+1]=std::byte(y+1);atlas[at+2]=std::byte(x%17+1);}
    files[model]=Tga(170,45);files[ammo]=Tga(24,24);files[ammo2]=Tga(24,24);files[face]=Tga(99,116);
}
void Configure()
{
    using namespace csol::client;
    engine.Con_DPrintf=Log;
    engine.pfnGetCvarFloat=[](const char*){return hudDraw;};
    engine.pfnGetLevelName=[]()->const char*{return "maps/zs_deadend.bsp";};
    engine.pfnGetCvarPointer=[](const char* name)->cvar_t*{return std::strcmp(name,"csol_hud_style")==0?&option:&scale;};
    api=&apiData;
    apiData.GetEngineType=[](){return ENGINE_GOLDSRC_HL25;};
    apiData.GetClientBase=[]()->void*{return reinterpret_cast<void*>(std::uintptr_t{0x10000000});};
    apiData.ResolveGameSymbol=[](void*,const char* name,mh_gamesymbol_kind_t kind,void** value)->mh_gamesymbol_status_t
    {
        Check(kind==MH_GAMESYMBOL_KIND_GLOBAL,"vitals resolve by matched global identity");
        *value=std::strcmp(name,"GoldCraftVitals_Health")==0?&healthGlobal:&armorGlobal;return MH_GAMESYMBOL_OK;
    };
    apiData.FindUserMsgHook=[](const char* name)->usermsg_t*{const auto it=messages.find(name);return it==messages.end()?nullptr:&it->second;};
    apiData.HookUserMsg=[](const char* name,pfnUserMsgHook hook)->pfnUserMsgHook
    {++installs;auto& entry=messages[name];auto old=entry.function;entry.function=hook;return old;};
    engine.pfnHookUserMsg=[](const char*,pfnUserMsgHook)->int{Check(false,"known native message must retain existing chain");return 1;};
    surface=&capture;extension=reinterpret_cast<IVGUI2Extension*>(std::uintptr_t{1});additiveSurface=true;
    privateFont=reinterpret_cast<HANDLE>(std::uintptr_t{1});fonts.fill(7);textFont=titleFont=7;
    option.value=1;scale.value=1;styleOption=&option;scaleOption=&scale;
    definition.id=400;definition.slot=0;definition.name="weapon_cannon";
    state.id=400;state.slot=0;state.clip=1;state.reserve=30;state.kind=csol::hud_style::AmmoKind::Magazine;
    character.id=1;view.health=1700;view.spectator=0;
    previousHealth=previousArmor=previousReset=previousInit=Previous;
    pendingVitals=false;nativeHealth=nativeArmor=nullptr;
    screenWidth=1280;screenHeight=720;layout=csol::hud_style::MakeLayout(1280,720,1);
}
void Seed()
{
    using namespace csol::client;
    auto hp=Bytes(1700,4),ap=Bytes(200,4);
    Check(Health("Health",int(hp.size()),hp.data())==73,"actual Health observer forwards original handler result");
    Check(Armor("Battery",int(ap.size()),ap.data())==73,"actual Battery observer forwards original handler result");
}
void Draw()
{
    using namespace csol::client;
    capture.quads.clear();capture.texts.clear();capture.fills.clear();BeginHudStyle(0,0);DrawHudStyle();Paint();
    Check(capture.pushes==capture.pops,"surface context always balanced");
}
std::string DigitsAt(int y)
{
    std::string result;
    for(const auto& q:capture.quads)if(q.y==y)
        for(unsigned digit=0;digit<csol::client::digits.size();++digit)
            if(q.texture==csol::client::digits[digit].id){Check(q.additive,"number uses additive surface path");result+=char('0'+digit);}
    return result;
}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};ref_params_t view{};
void SynchronizeSession(){++fixture::syncs;if(fixture::newSession){fixture::newSession=false;ResetHudStyle();}}
std::vector<std::byte> ReadResource(const char* path)
{++fixture::reads;const auto it=fixture::files.find(path);if(it==fixture::files.end())throw std::runtime_error("controlled missing resource");return it->second;}
const WeaponDefinition* FindWeaponDefinition(int id){return id==fixture::definition.id?&fixture::definition:nullptr;}
hud_style::WeaponState CurrentWeaponHudState(){return fixture::state;}
const Character* ViewCharacter(){return fixture::haveCharacter?&fixture::character:nullptr;}
int ScenarioHudLevel(){return fixture::scenarioLevel;}
}
int main(int argc,char** argv)
{
    using namespace csol::client;using namespace fixture;using csol::hud_style::Art;
    Configure();SetupResources();Seed();
    // Download not complete: real loader must release partial resources and
    // leave the old HUD available, rather than hiding it on cvar alone.
    files.erase(csol::hud_style::Resources[2].path);Prepare();BeginHudStyle(0,0);
    Check(!loaded&&!HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&capture.live.empty(),"incomplete fixed art falls back atomically");
    const auto attempts=reads;Prepare();Check(reads==attempts,"global resource retry bounded");
    SetupResources();retryAfter=0;Prepare();Check(loaded,"valid complete catalog and TGA art prepared");
    for(unsigned d=0;d<10;++d)
    {
        const auto& image=capture.uploads.at(digits[d].id);Check(image.Width==17&&image.Height==22,"each additive digit has its own complete texture");
        for(unsigned y=0;y<22;++y)for(unsigned x=0;x<17;++x)
        {const auto at=(y*17+x)*4;Check(image.RGBA[at]==x+1&&image.RGBA[at+1]==y+1&&image.RGBA[at+2]==d+1,"digit extraction retains exact TGA cell and row");}
    }
    PrepareSelection();Check(weaponImage.id&&ammoImage.id&&portrait.id,"complete selected row and character art");
    Draw();Check(HudStyleVitalsVisible()&&HudStyleWeaponVisible(),"valid alive native view owns both replacements");
    for(const auto& q:capture.quads)
    {
        if(q.texture==weaponImage.id||q.texture==ammoImage.id||q.texture==art[unsigned(Art::Health)].id||q.texture==art[unsigned(Art::Armor)].id)
            Check(q.additive,"original SPR art uses additive blend entry");
        if(q.texture==portrait.id||q.texture==art[unsigned(Art::CharacterBottom)].id||q.texture==art[unsigned(Art::WeaponCard)].id)
            Check(!q.additive,"original TGA panel and portrait use ordinary alpha path");
        if(q.texture==art[unsigned(Art::CharacterBottom)].id||q.texture==art[unsigned(Art::WeaponBottom)].id)
            Check(q.bottom==712&&q.color[3]==225,"original panels keep bottom gap and opacity");
        if(q.texture==portrait.id)Check(q.y==564&&q.u0==0,"native rectangular portrait preserves full U range");
    }
    Delete(portrait);files[face]=Tga(116,116);portraitRetryAfter=0;PrepareSelection();Draw();
    for(const auto& q:capture.quads)if(q.texture==portrait.id)
        Check(std::abs(q.u0-(1.f-99.f/116.f))<.00001f&&q.right-q.x==99,"square portrait uses original right-aligned 99/116 crop");
    Check(HudStyleLevelVisible(),"scenario level replaces old badge");
    Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& t){return t.value.find(L"黑龙炮")!=std::wstring::npos;}),"actual paint uses localized official weapon title");
    Check(DigitsAt(681).starts_with("1700200"),"actual paint retains wide health and armor digits on original H minus 39 baseline");
    Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& t){return t.value==L"1 黑龙炮"&&t.x==1114&&t.y==575;}),"original card title contains slot and official name at original anchor");
    Check(capture.fills.size()==1&&capture.fills[0].x==1174&&capture.fills[0].y==681&&capture.fills[0].right==1176&&capture.fills[0].bottom==703&&capture.fills[0].color==std::array<int,4>{75,75,75,255},"original two-pixel gray ammo divider");
    for(const auto& q:capture.quads)
    {
        if(q.texture==art[unsigned(Art::Health)].id)Check(q.x==9&&q.y==686,"original health icon anchor");
        if(q.texture==art[unsigned(Art::Armor)].id)Check(q.x==160&&q.y==686,"original armor icon anchor");
        if(q.texture==weaponImage.id)Check(q.x==1106&&q.y==586&&q.right==1276&&q.bottom==631,"original weapon card sprite remains 170 by 45 at top plus 15");
        if(q.texture==ammoImage.id)Check(q.x==1248&&q.y==679,"original main ammo icon anchor");
        if(q.texture==digits[1].id&&q.y==681&&q.x==28)Check(q.right-q.x==17&&q.bottom-q.y==22,"original digit size remains fixed");
    }
    state.secondary=25;Draw();
    Check(DigitsAt(681).ends_with("25"),"second ammunition uses same baseline");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==secondaryImage.id&&q.x==1074&&q.y==679&&q.additive;}),"second ammo icon uses original left anchor");
    state.secondary=255;Draw();Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==art[unsigned(Art::Infinite)].id&&q.additive;}),"second ammo 255 uses original infinity SPR");state.secondary=0;
    auto hp=Bytes(0,4);Health("Health",4,hp.data());Draw();
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==digits[0].id&&q.x==28&&q.y==681&&q.color==std::array<int,4>{75,75,75,255};}),"zero uses original gray digit tint");
    hp=Bytes(65536,4);Health("Health",4,hp.data());Draw();Check(DigitsAt(681).starts_with("65536200"),"wide HP above short reaches paint");
    hp=Bytes(0x80000000u,4);const auto oldRejected=rejected;Health("Health",4,hp.data());
    Check(vitals.health==65536&&rejected==oldRejected+1,"bad LONG neither mutates nor suppresses old message chain");
    for(int hide:{1,4,8}){BeginHudStyle(hide,0);Check(HudStyleVitalsVisible()==!(hide&12),"HideWeapon health bits preserved");Check(HudStyleWeaponVisible()==!(hide&5),"HideWeapon ammo bits preserved");}
    BeginHudStyle(0,1);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"intermission keeps gameplay overlay off");
    view.spectator=1;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"spectator does not inherit local alive HUD");view.spectator=0;
    view.health=0;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"dead player does not show live overlay");view.health=1700;
    for(bool* gate:{&gameUI,&console}){*gate=true;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"menu and console retain original ownership");*gate=false;}
    hudDraw=0;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"hud_draw0 honored");hudDraw=1;
    option.value=0;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"disabled style returns both old HUD panels");option.value=1;
    capture.root=0;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"missing VGUI root cannot consume native HUD");capture.root=1;
    additiveSurface=false;BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"unsupported additive surface keeps original HUD");additiveSurface=true;
    layout={};BeginHudStyle(0,0);Check(!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"zero viewport cannot consume native HUD");layout=csol::hud_style::MakeLayout(1280,720,1);
    BeginHudStyle(0,0);ConstrainHudStyle(false,true);Check(!HudStyleVitalsVisible()&&HudStyleWeaponVisible(),"unclassified native vitals fall back independently");
    BeginHudStyle(0,0);ConstrainHudStyle(true,false);Check(HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"unclassified native ammo falls back independently");
    scenarioLevel=-1;BeginHudStyle(0,0);Check(!HudStyleLevelVisible(),"ordinary server does not invent level");scenarioLevel=121;
    definition.name="weapon_unlisted";PrepareSelection();BeginHudStyle(0,0);
    Check(HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&!weaponImage.id,"unknown weapon falls back independently of vitals");
    definition.name="weapon_cannon";files.erase(ammo);PrepareSelection();BeginHudStyle(0,0);
    Check(!weaponImage.id&&!ammoImage.id&&!HudStyleWeaponVisible(),"missing selected ammo cannot partially own weapon HUD");
    const auto rowReads=reads;PrepareSelection();Check(reads==rowReads,"failed selection does not read every frame");
    files[ammo]={std::byte{1},std::byte{2}};selectionRetryAfter=0;PrepareSelection();BeginHudStyle(0,0);
    Check(!weaponImage.id&&!ammoImage.id&&!HudStyleWeaponVisible(),"corrupt TGA rejected by real decoder without partial replacement");
    files[ammo]=Tga(24,24);selectionRetryAfter=0;PrepareSelection();BeginHudStyle(0,0);Check(HudStyleWeaponVisible(),"downloaded weapon row recovers");
    state.id=999;PrepareSelection();BeginHudStyle(0,0);Check(!HudStyleWeaponVisible()&&!weaponImage.id,"switch to unregistered weapon retires prior art");state.id=400;PrepareSelection();
    state.kind=csol::hud_style::AmmoKind::Total;state.total=30;Draw();Check(DigitsAt(681).ends_with("30"),"total-ammo weapon keeps all thirty throws");
    for(const auto& q:capture.quads)if(q.y==681&&q.x>1000&&(q.texture==digits[3].id||q.texture==digits[0].id))
        Check(q.color==std::array<int,4>{255,255,255,255},"ordinary total-ammo weapon keeps white digits");
    state.specialAmmo=true;Draw();
    for(const auto& q:capture.quads)if(q.y==681&&q.x>1000&&(q.texture==digits[3].id||q.texture==digits[0].id))
        Check(q.color==std::array<int,4>{114,197,255,255},"explicit special ammunition uses original blue digits");
    state.specialAmmo=false;
    state.infinite=true;Draw();Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==art[unsigned(Art::Infinite)].id&&q.additive;}),"authoritative infinite state uses original infinity SPR");state.infinite=false;
    state.kind=csol::hud_style::AmmoKind::None;Draw();Check(DigitsAt(681)=="65536200","melee has title and art without phantom ammo");
    Reset("ResetHUD",0,nullptr);BeginHudStyle(0,0);Check(!vitals.healthKnown&&!vitals.armorKnown&&!HudStyleVitalsVisible(),"round reset forgets prior vitals");
    Seed();newSession=true;hp=Bytes(2000,4);Health("Health",4,hp.data());Check(vitals.health==2000&&vitals.healthKnown&&!vitals.armorKnown,"new session retires old armor before first HP");
    int authoritativeHealth=70000,authoritativeArmor=1024;nativeHealth=&authoritativeHealth;nativeArmor=&authoritativeArmor;
    HudStyleFrame();Check(vitals.health==70000&&vitals.armor==1024,"matched native globals seed missed signon values without byte clamp");
    nativeHealth=nativeArmor=nullptr;Init("InitHUD",0,nullptr);Check(!vitals.healthKnown&&!vitals.armorKnown,"InitHUD life state cleared");
    HudStyleVideoInit();Check(!loaded&&capture.live.empty()&&!HudStyleVitalsVisible()&&!HudStyleWeaponVisible(),"video reload deletes every owned texture and releases old HUD");
    message_chain::installed.clear();messages.clear();
    for(const char* name:{"Health","Battery","ResetHUD","InitHUD"})messages[name].function=Previous;
    InitializeHudStyle();
    Check(installs==2&&messages["Health"].function==Previous&&messages["Battery"].function==Previous,"Health and Battery observers deferred until after native HUD init");
    HudStyleFrame();Check(installs==4&&messages["Health"].function==Health&&messages["Battery"].function==Armor,"first frame installs two actual observers");
    Check(vitals.health==1900&&vitals.armor==400,"deferred install seeds exact wide native globals");
    auto packet=Bytes(1800,4);const auto calls=forwards;
    Check(messages["Health"].function("Health",4,packet.data())==73&&forwards==calls+1&&vitals.health==1800,"engine head executes observer then preserved original handler");
    auto heads=CaptureChainedUserMessageHeads();RefreshChainedUserMessageHeads(heads);InitializeHudStyle();HudStyleFrame();
    Check(installs==4,"video/init without downstream replacement does not wrap the same handlers twice");
    heads=CaptureChainedUserMessageHeads();messages["Health"].function=NativeReplacement;RefreshChainedUserMessageHeads(heads);
    InitializeHudStyle();HudStyleFrame();
    Check(installs==5&&previousHealth==NativeReplacement,"observed stock HUD re-registration replaces only changed chain");
    Check(messages["Health"].function("Health",4,packet.data())==91,"new native handler remains callable without self recursion");
    HudStyleVideoInit();Check(capture.live.empty(),"reinitialized resources also released");
    if(argc==2)
    {
        files.clear();const std::filesystem::path root=argv[1];
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file())
        {
            std::ifstream input(entry.path(),std::ios::binary);
            const std::vector<char> value(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>{});
            Put(entry.path().lexically_relative(root).generic_string(),std::string(value.begin(),value.end()));
        }
        Prepare();Check(loaded&&catalog.weapons.size()==34&&catalog.portraits.size()==149,"real generated catalog decoded with actual production parser");
        previousHealth=previousArmor=Previous;
        nativeHealth=nativeArmor=nullptr;Seed();state.kind=csol::hud_style::AmmoKind::Magazine;
        for(const auto& [name,row]:catalog.weapons)
        {
            definition.name=name;PrepareSelection();Draw();
            Check(weaponImage.id&&HudStyleWeaponVisible()==!row.ammo.empty(),"each exported weapon uses actual loader and replacement gate");
            if(row.ammo.empty()){state.kind=csol::hud_style::AmmoKind::None;Draw();Check(HudStyleWeaponVisible(),"melee without ammo retains title and silhouette");state.kind=csol::hud_style::AmmoKind::Magazine;}
        }
        for(const auto& [id,path]:catalog.portraits){character.id=id;PrepareSelection();Check(portrait.id>0,"every generated portrait decodes through real TGA loader");}
        for(int id:{39,40}){character.id=id;PrepareSelection();BeginHudStyle(0,0);Check(!portrait.id&&HudStyleVitalsVisible(),"missing original portrait never substitutes wrong person or removes vitals");}
        HudStyleVideoInit();Check(capture.live.empty(),"real catalog texture lifecycle has no outstanding ownership");
    }
    privateFont=nullptr;extension=nullptr;surface=nullptr;
    std::cout<<"hud_style_client checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
