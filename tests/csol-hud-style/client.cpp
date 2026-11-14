// SPDX-License-Identifier: GPL-3.0-or-later
// Entire production consumer and real TGA codec. Engine, surface and font
// handles are controlled boundaries; this is not a GL/font pixel test.
#include "allocation_probe.hpp"
#include "native/csol/client/hud_style.cpp"
#include "native/csol/client/resource_font_memory.hpp"
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
float clientTime=100;
int scenarioLevel=121;
std::map<std::string,std::vector<std::byte>> files;
csol::WeaponDefinition definition{};
const csol::WeaponDefinition* titleDefinition{};
std::map<int,csol::WeaponDefinition> selectionDefinitions;
std::vector<int> selectionItems;
int selectionPending{};
bool selectionHudOwner{};
std::map<int,int> selectionReserve;
std::set<int> selectionEmpty;
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
std::vector<std::byte> MoneyPacket(std::uint32_t value,unsigned track=1)
{auto bytes=Bytes(value,4);bytes.push_back(std::byte(track));return bytes;}
void SendMoney(std::uint32_t value,unsigned track=1)
{
    auto bytes=MoneyPacket(value,track);const auto before=forwards;
    Check(csol::client::MoneyMessage("Money",int(bytes.size()),bytes.data())==73&&forwards==before+1,
        "Money observer forwards exactly once and preserves original result");
}
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
const std::string json=R"({"version":1,"weapons":[{"name":"weapon_cannon","title":"黑龙炮","weapon":"resource/csol/hud/v1/cannon.tga","ammo":"resource/csol/hud/v1/ammo.tga","ammo2":"resource/csol/hud/v1/ammo2.tga"},{"name":"weapon_k3","title":"K3","weapon":"resource/csol/hud/v1/k3_weapon.tga","ammo":"resource/csol/hud/v1/k3_ammo.tga","ammo2":""},{"name":"weapon_tknifeex2","title":"三重战术飞刀","weapon":"resource/csol/hud/v1/tknifeex2_weapon.tga","ammo":"resource/csol/hud/v1/tknifeex2_ammo.tga","ammo2":""}],"portraits":[{"id":1,"path":"resource/csol/hud/v1/p1.tga"}]})";
struct Quad {int texture{},x{},y{},right{},bottom{};float u0{},u1{};std::array<int,4> color{};bool additive{};};
struct Text {std::wstring value;int x{},y{};vgui::HFont font{};std::array<int,4> color{};};
struct Filled {int x{},y{},right{},bottom{};std::array<int,4> color{};};
struct FontRecipe {vgui::HFont handle{};std::string family;int tall{},weight{},blur{},scanlines{},flags{},low{},high{};};
struct Surface final:vgui::HudTestSurface
{
    bool record=true,glyphsReady=true;
    int width=1280,height=720,current{},next=10,pushes{},pops{},root=1,x{},y{};
    std::array<int,4> color{};
    std::array<int,4> textColor{};
    vgui::HFont currentFont{};
    unsigned textMeasurements{},fontCreations{};
    std::set<int> live;
    std::map<int,csol::Image> uploads;
    std::vector<Quad> quads;
    std::vector<Text> texts;
    std::vector<Filled> fills;
    std::vector<FontRecipe> fontRecipes;
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
    void DrawFilledRect(int x0,int y0,int x1,int y1)override{if(record)fills.push_back({x0,y0,x1,y1,color});}
    void DrawTexturedPolygon(vgui::VGuiVertex* v,int count)override
    {Check(count==4,"HUD uses four vertices");if(record)quads.push_back({current,v[0].GetX(),v[0].GetY(),v[2].GetX(),v[2].GetY(),v[0].GetU(),v[2].GetU(),color});}
    void DrawTexturedRectAdd(int x0,int y0,int x1,int y1)override
    {if(record)quads.push_back({current,x0,y0,x1,y1,0,1,color,true});}
    vgui::HFont CreateFont()override{++fontCreations;return 99;}
    bool AddGlyphSetToFont(vgui::HFont handle,const char* family,int tall,int weight,int blur,int scanlines,int flags,int low,int high)override
    {fontRecipes.push_back({handle,family,tall,weight,blur,scanlines,flags,low,high});return glyphsReady;}
    int GetFontTall(vgui::HFont handle)override{return handle==99?15:24;}
    void GetTextSize(vgui::HFont,const wchar_t* t,int& w,int& h)override{++textMeasurements;w=int(std::wcslen(t))*9;h=13;}
    void DrawSetTextFont(vgui::HFont font)override{currentFont=font;}
    void DrawSetTextColor(int r,int g,int b,int a)override{textColor={r,g,b,a};}
    void DrawSetTextPos(int a,int b)override{x=a;y=b;}
    void DrawPrintText(const wchar_t* t,int n)override{if(record)texts.push_back({std::wstring(t,n),x,y,currentFont,textColor});}
} capture;
struct Scheme final:vgui::IScheme
{
    std::string revision="1";
    vgui::HFont font=7;
    unsigned fontRequests{};
    const char* GetResourceString(const char* name)override
    {Check(std::strcmp(name,"CSOLHudRevision")==0,"scheme identity checked through private revision key");return revision.c_str();}
    vgui::IBorder* GetBorder(const char*)override{return nullptr;}
    vgui::HFont GetFont(const char* name,bool proportional)override
    {++fontRequests;Check(std::strcmp(name,"EngineFont")==0&&proportional,"original EngineFont requested with proportional=true");return font;}
    Color GetColor(const char*,Color fallback)override{return fallback;}
} scheme;
struct SchemeManager final:vgui::ISchemeManager
{
    vgui::HScheme result=19;
    bool available=true;
    unsigned loads{};
    vgui::HScheme LoadSchemeFromFile(const char* path,const char* tag)override
    {++loads;Check(std::strcmp(path,csol::hud_style::SchemePath)==0&&std::strcmp(tag,"CSOLHudV1")==0,"only isolated original-font scheme loaded");return result;}
    void ReloadSchemes()override{Check(false,"HUD must not reload unrelated global schemes");}
    vgui::HScheme GetDefaultScheme()override{Check(false,"HUD must not silently use default scheme");return 0;}
    vgui::HScheme GetScheme(const char*)override{return result;}
    vgui::IImage* GetImage(const char*,bool)override{return nullptr;}
    vgui::HTexture GetImageID(const char*,bool)override{return 0;}
    vgui::IScheme* GetIScheme(vgui::HScheme handle)override
    {Check(handle==result&&handle!=0,"font resolved through loaded private scheme handle");return available?&scheme:nullptr;}
    void Shutdown(bool)override{Check(false,"HUD must not shut down shared VGUI schemes");}
    int GetProportionalScaledValue(int value)override{return value;}
    int GetProportionalNormalizedValue(int value)override{return value;}
} schemeManager;
cvar_t option{},scale{};
void SetupResources()
{
    files.clear();Put(csol::hud_style::CatalogPath,json);
    // Scheme parsing/rasterization belongs to the real VGUI interface. This
    // fixture supplies bytes for the consumer's download/readiness gate only.
    Put(csol::hud_style::SchemePath,"controlled scheme bytes");
    for(const auto& r:csol::hud_style::Resources)files[r.path]=Tga(r.width,r.height);
    auto& atlas=files[csol::hud_style::Resources[unsigned(csol::hud_style::Art::WhiteDigits)].path];
    for(unsigned y=0;y<22;++y)for(unsigned x=0;x<170;++x)
    {const auto at=18+(y*170+x)*4;atlas[at]=std::byte(x/17+1);atlas[at+1]=std::byte(y+1);atlas[at+2]=std::byte(x%17+1);}
    auto& moneyAtlas=files[csol::hud_style::Resources[unsigned(csol::hud_style::Art::MoneyDigits)].path];
    for(unsigned y=0;y<18;++y)for(unsigned x=0;x<130;++x)
    {const auto at=18+(y*130+x)*4;moneyAtlas[at]=std::byte(x/13+1);moneyAtlas[at+1]=std::byte(y+1);moneyAtlas[at+2]=std::byte(x%13+1);}
    files[model]=Tga(170,45);files[ammo]=Tga(24,24);files[ammo2]=Tga(24,24);files[face]=Tga(99,116);
    for(const char* name:{"k3","tknifeex2"})
    {files[std::string("resource/csol/hud/v1/")+name+"_weapon.tga"]=Tga(170,45);files[std::string("resource/csol/hud/v1/")+name+"_ammo.tga"]=Tga(24,24);}
}
void Configure()
{
    using namespace csol::client;
    engine.Con_DPrintf=Log;
    engine.pfnGetCvarFloat=[](const char*){return hudDraw;};
    engine.GetClientTime=[](){return clientTime;};
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
    schemes=&schemeManager;privateFont=reinterpret_cast<HANDLE>(std::uintptr_t{1});titleFont=7;
    option.value=1;scale.value=1;styleOption=&option;scaleOption=&scale;
    definition.id=400;definition.slot=0;definition.name="weapon_cannon";
    state.id=400;state.slot=0;state.clip=1;state.reserve=30;state.kind=csol::hud_style::AmmoKind::Magazine;
    character.id=1;view.health=1700;view.spectator=0;
    previousHealth=previousArmor=previousMoney=previousReset=previousInit=Previous;
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
void InvalidateSelectedRow()
{
    using namespace csol::client;
    if(auto found=selectionCards.find(state.id);found!=selectionCards.end())
    {
        auto& card=found->second;
        ReleaseAmmoFrames(card);Delete(card.image);Delete(card.ammo);Delete(card.secondary);
        card.equippedReady=false;card.imageRetryAfter=0;card.titleId=0;
    }
}
std::string DigitsAt(int y)
{
    std::string result;
    for(const auto& q:capture.quads)if(q.y==y)
        for(unsigned digit=0;digit<csol::client::digits.size();++digit)
            if(q.texture==csol::client::digits[digit].id){Check(q.additive,"number uses additive surface path");result+=char('0'+digit);}
    return result;
}
std::string MoneyAt(int y)
{
    std::string result;
    for(const auto& q:capture.quads)if(q.y==y)
        for(unsigned digit=0;digit<csol::client::moneyDigits.size();++digit)
            if(q.texture==csol::client::moneyDigits[digit].id)
            {Check(q.additive,"money digits use original additive path");result+=char('0'+digit);}
    return result;
}
void MoneyTint(const std::array<int,4>& expected,const char* reason)
{
    using namespace csol::client;
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
        {return q.texture==art[unsigned(csol::hud_style::Art::MoneyDollar)].id&&q.color==expected;}),reason);
}
std::vector<Quad> MoneyQuads()
{
    using namespace csol::client;using csol::hud_style::Art;
    std::vector<Quad> result;
    for(const auto& q:capture.quads)
        if(std::any_of(moneyDigits.begin(),moneyDigits.end(),[&](const auto& t){return t.id==q.texture;})||
            std::any_of(art.begin()+unsigned(Art::MoneyBackground),art.end(),[&](const auto& t){return t.id==q.texture;}))
            result.push_back(q);
    return result;
}
bool SameMoneyGeometry(const std::vector<Quad>& before)
{
    const auto after=MoneyQuads();
    return before.size()==after.size()&&std::equal(before.begin(),before.end(),after.begin(),[](const auto& a,const auto& b)
        {return a.texture==b.texture&&a.x==b.x&&a.y==b.y&&a.right==b.right&&a.bottom==b.bottom;});
}
void ExerciseMoney()
{
    using namespace csol::client;using csol::hud_style::Art;
    PrepareMoney();Check(moneyLoaded,"independent money resources prepared");
    for(unsigned d=0;d<10;++d)
    {
        const auto& image=capture.uploads.at(moneyDigits[d].id);
        Check(image.Width==13&&image.Height==18,"money glyph keeps original 13 by 18 size");
        for(unsigned y=0;y<18;++y)for(unsigned x=0;x<13;++x)
        {const auto at=(y*13+x)*4;Check(image.RGBA[at]==x+1&&image.RGBA[at+1]==y+1&&image.RGBA[at+2]==d+1,"money slice retains exact source cell and rows");}
    }
    Draw();Check(!HudStyleMoneyVisible(),"no fabricated account before first Money message");
    SendMoney(7500);Draw();
    Check(HudStyleMoneyVisible()&&MoneyAt(200)=="7500"&&MoneyAt(227).empty(),"first signon balance draws without fabricated income");
    MoneyTint({255,255,255,255},"first signon balance does not start original change brightness");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==art[unsigned(Art::MoneyBackground)].id&&q.x==0&&q.y==195&&q.right==191&&q.bottom==223&&!q.additive;}),
        "scenario money panel uses original alpha TGA at left y195");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==art[unsigned(Art::MoneyDollar)].id&&q.x==51&&q.y==200&&q.additive;}),"dollar uses original x51 additive glyph");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==moneyDigits[7].id&&q.x==82&&q.y==200&&q.right==95&&q.bottom==218;}),"four-digit account reserves original five-cell field");
    const auto stable=MoneyQuads();
    const auto savedState=state;const auto savedDefinition=definition.name;
    state.id=999;Draw();Check(!HudStyleWeaponVisible()&&SameMoneyGeometry(stable),"money stays fixed when weapon message precedes selection preparation");
    PrepareSelection();Draw();Check(SameMoneyGeometry(stable),"unknown weapon cannot move money panel");
    state=savedState;definition.name="weapon_unlisted";PrepareSelection();Draw();
    Check(!HudStyleWeaponVisible()&&SameMoneyGeometry(stable),"uncatalogued weapon cannot move money panel");
    definition.name=savedDefinition;auto savedAmmo=files.at(ammo);files.erase(ammo);InvalidateSelectedRow();PrepareSelection();Draw();
    Check(!HudStyleWeaponVisible()&&SameMoneyGeometry(stable),"missing weapon ammo art cannot move money panel");
    files[ammo]={std::byte{1}};InvalidateSelectedRow();PrepareSelection();Draw();
    Check(!HudStyleWeaponVisible()&&SameMoneyGeometry(stable),"corrupt weapon ammo art cannot move money panel");
    files[ammo]=std::move(savedAmmo);InvalidateSelectedRow();PrepareSelection();
    state.kind=csol::hud_style::AmmoKind::None;Draw();
    Check(HudStyleWeaponVisible()&&SameMoneyGeometry(stable),"melee without ammunition keeps independent account geometry");state=savedState;
    scenarioLevel=-1;Draw();Check(MoneyAt(159)=="7500"&&MoneyAt(200).empty(),"ordinary mode removes original 41-pixel scenario offset");scenarioLevel=121;
    for(float value:{.5f,1.f,2.f})
    {
        layout=csol::hud_style::MakeLayout(1280,720,value);Draw();
        const float s=layout.scale;
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==art[unsigned(Art::MoneyBackground)].id&&q.x==0&&q.y==int(195*s)&&q.right==int(191*s)&&q.bottom==int(223*s);}),
            "money uses same whole-HUD scale without weapon-dependent anchor");
    }
    layout=csol::hud_style::MakeLayout(1280,720,1);
    clientTime=110;SendMoney(9000);Draw();Check(MoneyAt(200)=="9000"&&MoneyAt(227)=="1500","tracked credit draws authoritative amount and signed difference");
    MoneyTint({160,201,82,255},"original credit age0 applies integer interpolation then228/255 brightness");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==art[unsigned(Art::MoneyPlus)].id&&q.x==56&&q.y==227&&q.additive;}),"credit symbol uses original offset below account");
    const auto savedMoney=HudStyleMoneyState();const auto savedTime=moneyChangedAt;const auto savedFade=moneyFadeActive;
    for(int length:{-1,0,1,2,3,4,6,7})
    {
        auto packet=MoneyPacket(12);packet.resize(8);const auto rejectedBefore=rejected,calls=forwards;
        Check(MoneyMessage("Money",length,packet.data())==73&&forwards==calls+1&&rejected==rejectedBefore+1,
            "bad Money length remains forwarded once through native chain");
        const auto value=HudStyleMoneyState();
        Check(value.amount==savedMoney.amount&&value.delta==savedMoney.delta&&value.known==savedMoney.known&&
            value.changed==savedMoney.changed&&value.trackChange==savedMoney.trackChange&&moneyChangedAt==savedTime&&moneyFadeActive==savedFade,
            "bad Money packet preserves balance, delta, flags and display clock");
    }
    const auto nullRejected=rejected;MoneyMessage("Money",5,nullptr);
    Check(rejected==nullRejected+1&&money.amount==9000&&moneyChangedAt==savedTime,"null Money cannot disturb valid account");
    clientTime=112.49f;Draw();Check(MoneyAt(227)=="1500","income remains before original 2.5-second cutoff");
    clientTime=112.5f;Draw();Check(MoneyAt(227).empty(),"income disappears at original 2.5-second cutoff");
    MoneyTint({139,154,111,255},"original credit halfway color keeps separate integer brightness truncation");
    clientTime=115;Draw();Check(MoneyAt(200)=="9000"&&MoneyAt(227).empty(),"account remains after original five-second color window");
    MoneyTint({255,255,255,255},"original five-second cutoff restores full white");
    Check(!moneyFadeActive,"completed original color window retires its active flag");
    clientTime=120;SendMoney(4250);Draw();Check(MoneyAt(227)=="4750","expense amount uses positive magnitude without signed-digit indexing");
    MoneyTint({206,23,23,255},"original expense age0 uses separately truncated brightness");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==art[unsigned(Art::MoneyMinus)].id&&q.x==56&&q.y==227&&q.additive;}),"expense uses minus symbol");
    clientTime=121;SendMoney(4250);Draw();
    Check(moneyChangedAt==120&&moneyFadeActive&&MoneyAt(227).empty(),"repeated balance cannot restart timer or fabricate delta while original fade remains active");
    MoneyTint({202,202,202,255},"repeated balance uses white accent at remaining original brightness");
    clientTime=125;Draw();MoneyTint({255,255,255,255},"repeated balance still expires at original timestamp plus five seconds");
    SendMoney(32000,0);Draw();Check(MoneyAt(200)=="32000"&&MoneyAt(227).empty(),"untracked server change cannot invent an income row");
    SendMoney(32000);Draw();Check(MoneyAt(227).empty(),"repeated balance cannot restart a delta");
    SendMoney(0xffffffffu);Draw();Check(money.known&&!HudStyleMoneyVisible()&&MoneyQuads().empty(),"negative protocol sentinel hides only account");
    SendMoney(7500);Draw();Check(HudStyleMoneyVisible()&&MoneyAt(227).empty(),"recovery from negative sentinel does not invent income");
    clientTime=130;SendMoney(0);Draw();Check(MoneyAt(200)=="0"&&MoneyAt(227)=="7500","zero account and full-balance purchase render safely");
    SendMoney(2147483647u);Draw();Check(MoneyAt(200)=="2147483647"&&MoneyAt(227)=="2147483647","maximum signed account and delta render without truncation");
    SendMoney(0);Draw();Check(MoneyAt(227)=="2147483647","maximum expense magnitude renders without overflow");
    // Download failures must not discard either already-working base panel.
    const int healthTexture=art[unsigned(Art::Health)].id,weaponTexture=weaponImage.id;
    for(unsigned i=unsigned(Art::MoneyBackground);i<unsigned(Art::Count);++i)
    {
        const auto& resource=csol::hud_style::Resources[i];const auto saved=files.at(resource.path);
        for(int failure:{0,1,2})
        {
            ReleaseMoney();moneyRetryAfter=0;
            if(failure==0)files.erase(resource.path);
            else if(failure==1)files[resource.path]={std::byte{1},std::byte{2}};
            else files[resource.path]=Tga(resource.width+1,resource.height);
            PrepareMoney();Draw();
            Check(!moneyLoaded&&!HudStyleMoneyVisible()&&loaded&&HudStyleVitalsVisible()&&HudStyleWeaponVisible()&&
                art[unsigned(Art::Health)].id==healthTexture&&weaponImage.id==weaponTexture,"missing, corrupt or wrongly sized money art falls back independently");
            const auto before=reads;PrepareMoney();Check(reads==before,"failed money resource does not read every frame");
            files[resource.path]=saved;moneyRetryAfter=0;PrepareMoney();Draw();
            Check(moneyLoaded&&HudStyleMoneyVisible(),"downloaded money art recovers through actual TGA loader");
        }
    }
    const auto baseCatalog=files.at(csol::hud_style::CatalogPath);ReleaseBase();retryAfter=0;files.erase(csol::hud_style::CatalogPath);
    Prepare();Draw();Check(!loaded&&moneyLoaded&&HudStyleMoneyVisible()&&!MoneyQuads().empty(),"missing base catalog does not suppress independently ready account");
    files[csol::hud_style::CatalogPath]=baseCatalog;retryAfter=0;Prepare();PrepareSelection();
    SendMoney(7500,0);Draw();
}
void ExerciseScheme()
{
    using namespace csol::client;
    const auto saved=files.at(csol::hud_style::SchemePath);
    HudStyleFrame();const auto warmLoads=schemeManager.loads;
    for(int frame=0;frame<5;++frame)HudStyleFrame();
    Check(schemeManager.loads==warmLoads,"ready HUD does not load a scheme every frame");
    Check(titleFont==99&&capture.fontCreations==1&&!capture.textMeasurements,"title uses one immutable current-original EngineFont recipe without shrink measurement");
    Check(capture.fontRecipes.size()==1&&capture.fontRecipes[0].family=="Sarasa UI SC"&&
        capture.fontRecipes[0].tall==14&&capture.fontRecipes[0].weight==600&&
        capture.fontRecipes[0].flags==vgui::FONTFLAG_DROPSHADOW&&capture.fontRecipes[0].blur==0&&
        capture.fontRecipes[0].scanlines==0&&capture.fontRecipes[0].low==0&&capture.fontRecipes[0].high==0xffff,
        "fixed768 yres branch submits14/600/shadow with no AA or second proportional scale");
    for(int failure:{0,1,2,3,4,5,6})
    {
        ReleaseBase();retryAfter=0;
        if(failure==0)files.erase(csol::hud_style::SchemePath);
        if(failure==1)files[csol::hud_style::SchemePath].clear();
        if(failure==2)schemeManager.result=0;
        if(failure==3)schemeManager.available=false;
        if(failure==4)scheme.revision="";
        if(failure==5)scheme.revision="0";
        if(failure==6)scheme.font=0;
        Prepare();Draw();
        Check(!loaded&&!HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&HudStyleMoneyVisible(),
            "missing scheme, default fallback or invalid font returns base HUD ownership without losing money");
        const auto attempts=schemeManager.loads;Prepare();Check(schemeManager.loads==attempts,"failed scheme retry is bounded");
        files[csol::hud_style::SchemePath]=saved;schemeManager.result=19;schemeManager.available=true;scheme.revision="1";scheme.font=7;
        retryAfter=0;HudStyleFrame();Draw();
        Check(loaded&&HudStyleVitalsVisible()&&HudStyleWeaponVisible()&&titleFont==99,"valid original scheme recovers after download or font readiness");
    }
    auto& title=catalog.weapons.at(definition.name).title;const auto savedTitle=title;
    title="这是用于验证原版字体不能被自动缩小的非常长武器名称";
    InvalidateSelectedRow();PrepareSelection();HudStyleFrame();Draw();
    Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& t)
        {return t.value==L"1 这是用于验证原版字体不能被自动缩小的非常长武器名称"&&t.font==99&&t.color==std::array<int,4>{255,255,255,255};}),
        "long official title keeps complete text and original font/color without adaptive shrinking");
    Check(capture.fontCreations==1&&!capture.textMeasurements,"long title reuses the immutable glyph recipe");
    title=savedTitle;InvalidateSelectedRow();PrepareSelection();
    const auto preparedFonts=capture.fontCreations,preparedRequests=scheme.fontRequests;
    for(const auto dimensions:{std::array<int,2>{640,480},{800,600},{1024,768},{1280,720},{1920,1080},{1920,1200},{2560,1440},{3840,2160}})
    {
        capture.width=dimensions[0];capture.height=dimensions[1];HudStyleFrame();Draw();
        Check(layout.scale==1,"normal viewport keeps original fixed-pixel HUD dimensions");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==weaponImage.id&&q.x==dimensions[0]-174&&q.y==dimensions[1]-134&&q.right-q.x==170&&q.bottom-q.y==45;}),
            "weapon art retains original pixel dimensions across supported resolutions");
        Check(titleFont==99&&capture.fontCreations==preparedFonts&&scheme.fontRequests==preparedRequests,
            "live resolution never reselects EngineFont yres or allocates another HFont");
    }
    capture.width=1280;capture.height=720;scale.value=.5f;HudStyleFrame();Draw();
    Check(layout.scale==.5f&&titleFont==99,"explicit image scaling does not synthesize a smaller EngineFont");
    scale.value=1;HudStyleFrame();
}
std::string WeaponDigits()
{
    std::string result;
    for(const auto& q:capture.quads)if(q.x>capture.width/2&&q.y==capture.height-39)
        for(unsigned digit=0;digit<csol::client::digits.size();++digit)
            if(q.texture==csol::client::digits[digit].id)result+=char('0'+digit);
    return result;
}
void ExerciseWeaponSamples()
{
    using namespace csol::client;
    const auto savedDefinition=definition;const auto savedState=state;
    definition.id=197;definition.name="weapon_k3";definition.slot=0;
    state={};state.id=197;state.kind=csol::hud_style::AmmoKind::Magazine;PrepareSelection();
    for(const auto values:{std::array<int,2>{100,2000},{100,1000},{0,2000},{150,200}})
    {
        state.clip=values[0];state.reserve=values[1];Draw();
        Check(HudStyleWeaponVisible()&&WeaponDigits()==std::to_string(values[0])+std::to_string(values[1]),
            "ordinary K3 magazine and wide scenario reserve remain separate exact values");
        const int reserveStart=1246-int(std::to_string(values[1]).size())*17;
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.y==681&&q.x==reserveStart&&q.texture==digits[std::to_string(values[1])[0]-'0'].id;}),
            "K3 reserve follows original variable-width right edge rather than a three-digit total field");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
            {return q.texture==ammoImage.id&&q.color==std::array<int,4>{255,255,255,255};}),"K3 primary ammo icon remains ordinary white");
        for(const auto& q:capture.quads)if(q.x>1176&&q.y==681)
            for(const auto& digit:digits)if(q.texture==digit.id)
                Check(q.color==std::array<int,4>{255,255,255,255}&&q.right-q.x==17&&q.bottom-q.y==22,
                    "nonzero K3 reserve stays white in original-sized glyphs even with an empty magazine");
        if(values[0]==0)Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
            {return q.texture==digits[0].id&&q.x==1153&&q.y==681&&q.color==std::array<int,4>{75,75,75,255};}),
            "empty K3 magazine grays only its zero, preserving reserve digits");
    }
    state.infinite=true;Draw();
    Check(WeaponDigits()=="150"&&capture.fills.size()==1,"reserve-only capability keeps K3 clip and divider while replacing reserve digits");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==art[unsigned(csol::hud_style::Art::Infinite)].id&&q.x==1184&&q.y==683&&q.right==1220&&q.bottom==707&&q.color==std::array<int,4>{255,255,255,255};}),
        "K3 reserve infinity uses original double-width x subtraction and y plus2");state.infinite=false;
    for(const auto sample:{std::pair{"weapon_cannon",144},std::pair{"weapon_tknifeex2",220}})
    {
        definition.id=sample.second;definition.name=sample.first;definition.slot=sample.second==220?1:0;
        state={};state.id=sample.second;state.kind=csol::hud_style::AmmoKind::Total;state.specialAmmo=true;
        state.total=sample.second==220?30:20;PrepareSelection();Draw();
        Check(HudStyleWeaponVisible()&&WeaponDigits()==std::to_string(state.total),"cannon and throwing knife show one complete special-ammo total");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
            {return q.texture==ammoImage.id&&q.color==std::array<int,4>{114,197,255,255};}),"both special weapon icons use the original blue tint");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==digits[state.total/10].id&&q.x==1205&&q.y==681;}),"two-digit special total ends at original W-17-iconWidth");
        state.infinite=true;Draw();
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
            {return q.texture==art[unsigned(csol::hud_style::Art::Infinite)].id&&q.x==1184&&q.y==683&&q.right==1220&&q.bottom==707&&q.color==std::array<int,4>{114,197,255,255};}),
            "special infinity preserves original 36 by24 geometry and independent blue tint");state.infinite=false;
        state.total=0;Draw();Check(WeaponDigits()=="0","special total zero does not invent a magazine or reserve field");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
            {return q.texture==digits[0].id&&q.x>1000&&q.y==681&&q.color==std::array<int,4>{75,75,75,255};}),"IDA-proven special zero uses gray75 in the shared digit font");
    }
    for(bool blue:{false,true})for(const auto value:{std::pair{1000,"000"},std::pair{1234,"234"}})
    {
        state.specialAmmo=blue;state.total=value.first;Draw();
        Check(WeaponDigits()==value.second,"original no-clip total retains only the final three digits for values above999");
        const std::array<int,4> color=blue?std::array<int,4>{114,197,255,255}:std::array<int,4>{255,255,255,255};
        for(const auto& q:capture.quads)if(q.y==681&&q.x>=1188&&q.x<1239)
            for(const auto& digit:digits)if(q.texture==digit.id)
                Check(q.color==color,"positive total1000 displays bright000 based on original value, not gray zero");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==digits[value.second[0]-'0'].id&&q.x==1188&&q.y==681;}),"three-digit total begins at original W-4*17-iconWidth");
    }
    state.id=999;Draw();Check(!HudStyleWeaponVisible()&&HudStyleVitalsVisible()&&HudStyleMoneyVisible(),"unknown weapon falls back while independent vitals and money remain visible");
    definition=savedDefinition;state=savedState;PrepareSelection();
}
}
#ifndef CSOL_INTERFACE_TEST
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
#endif
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};ref_params_t view{};
void SynchronizeSession(){++fixture::syncs;if(fixture::newSession){fixture::newSession=false;ResetHudStyle();}}
std::vector<std::byte> ReadResource(const char* path)
{++fixture::reads;const auto it=fixture::files.find(path);if(it==fixture::files.end())throw std::runtime_error("controlled missing resource");return it->second;}
#ifndef CSOL_INVENTORY_TEST
const WeaponDefinition* FindWeaponDefinition(int id)
{if(id==fixture::definition.id)return &fixture::definition;
 const auto it=fixture::selectionDefinitions.find(id);return it==fixture::selectionDefinitions.end()?nullptr:&it->second;}
WeaponSelectionView WeaponSelectionForHud(){return {fixture::selectionPending,fixture::selectionItems};}
void SetWeaponSelectionHudVisible(bool visible){fixture::selectionHudOwner=visible;}
void SetWeaponSelectionHudAvailable(bool){}
bool WeaponHasAmmo(int id){return !fixture::selectionEmpty.contains(id);}
int WeaponReserveAmmo(int type){const auto it=fixture::selectionReserve.find(type);return it==fixture::selectionReserve.end()?0:it->second;}
const WeaponDefinition* FindWeaponTitleDefinition(int id)
{return fixture::titleDefinition?fixture::titleDefinition:FindWeaponDefinition(id);}
#endif
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
    Check(std::none_of(capture.texts.begin(),capture.texts.end(),[](const auto& t){return t.value.starts_with(L"Lv.");})&&DigitsAt(661).empty(),
        "modern HUD does not synthesize Lv text or replace the original level digit layer");
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
    ExerciseMoney();ExerciseScheme();ExerciseWeaponSamples();
    state.secondary=25;Draw();
    Check(DigitsAt(681).ends_with("25"),"second ammunition uses same baseline");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==secondaryImage.id&&q.x==1074&&q.y==679&&q.additive&&q.color==std::array<int,4>{114,197,255,255};}),"second ammo icon uses original left anchor and blue tint");
    state.secondary=255;Draw();Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==art[unsigned(Art::Infinite)].id&&q.additive&&q.x==1038&&q.y==681&&q.right==1074&&q.bottom==705&&q.color==std::array<int,4>{114,197,255,255};}),"second ammo255 uses original infinity anchor without main-ammo y offset");state.secondary=0;
    auto hp=Bytes(0,4);Health("Health",4,hp.data());Draw();
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==digits[0].id&&q.x==28&&q.y==681&&q.color==std::array<int,4>{75,75,75,255};}),"zero uses original gray digit tint");
    hp=Bytes(65536,4);Health("Health",4,hp.data());Draw();Check(DigitsAt(681).starts_with("65536200"),"wide HP above short reaches paint");
    hp=Bytes(0x80000000u,4);const auto oldRejected=rejected;Health("Health",4,hp.data());
    Check(vitals.health==65536&&rejected==oldRejected+1,"bad LONG neither mutates nor suppresses old message chain");
    for(int hide:{1,4,8,32})
    {
        BeginHudStyle(hide,0);Check(HudStyleVitalsVisible()==!(hide&12),"HideWeapon health bits preserved");
        Check(HudStyleWeaponVisible()==!(hide&5),"HideWeapon ammo bits preserved");
        Check(HudStyleMoneyVisible()==!(hide&36),"HideWeapon money and all bits are independent of weapon and health bits");
    }
    const auto allHidden=[](){return !HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&!HudStyleMoneyVisible();};
    BeginHudStyle(0,1);Check(allHidden(),"intermission keeps gameplay overlay off");
    view.spectator=1;BeginHudStyle(0,0);Check(allHidden(),"spectator does not inherit local alive HUD");view.spectator=0;
    view.health=0;BeginHudStyle(0,0);Check(allHidden(),"dead player does not show live overlay");view.health=1700;
    for(bool* gate:{&gameUI,&console}){*gate=true;BeginHudStyle(0,0);Check(allHidden(),"menu and console retain original ownership");*gate=false;}
    hudDraw=0;BeginHudStyle(0,0);Check(allHidden(),"hud_draw0 honored");hudDraw=1;
    option.value=0;BeginHudStyle(0,0);Check(allHidden(),"disabled style returns every old HUD panel");option.value=1;
    capture.root=0;BeginHudStyle(0,0);Check(allHidden(),"missing VGUI root cannot consume native HUD");capture.root=1;
    additiveSurface=false;BeginHudStyle(0,0);Check(allHidden(),"unsupported additive surface keeps original HUD");additiveSurface=true;
    layout={};BeginHudStyle(0,0);Check(allHidden(),"zero viewport cannot consume native HUD");layout=csol::hud_style::MakeLayout(1280,720,1);
    BeginHudStyle(0,0);ConstrainHudStyle(false,true,true);Check(!HudStyleVitalsVisible()&&HudStyleWeaponVisible()&&HudStyleMoneyVisible(),"unclassified native vitals fall back independently");
    BeginHudStyle(0,0);ConstrainHudStyle(true,false,true);Check(HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&HudStyleMoneyVisible(),"unclassified native ammo falls back independently");
    BeginHudStyle(0,0);ConstrainHudStyle(true,true,false);Check(HudStyleVitalsVisible()&&HudStyleWeaponVisible()&&!HudStyleMoneyVisible(),"unclassified native currency falls back independently");
    scenarioLevel=-1;Draw();Check(std::none_of(capture.texts.begin(),capture.texts.end(),[](const auto& t){return t.value.starts_with(L"Lv.");}),"ordinary server does not invent a level label");scenarioLevel=121;
    definition.name="weapon_unlisted";PrepareSelection();BeginHudStyle(0,0);
    Check(HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&!weaponImage.id,"unknown weapon falls back independently of vitals");
    definition.name="weapon_cannon";files.erase(ammo);InvalidateSelectedRow();PrepareSelection();BeginHudStyle(0,0);
    Check(!weaponImage.id&&!ammoImage.id&&!HudStyleWeaponVisible(),"missing selected ammo cannot partially own weapon HUD");
    const auto rowReads=reads;PrepareSelection();Check(reads==rowReads,"failed selection does not read every frame");
    files[ammo]={std::byte{1},std::byte{2}};InvalidateSelectedRow();PrepareSelection();BeginHudStyle(0,0);
    Check(!weaponImage.id&&!ammoImage.id&&!HudStyleWeaponVisible(),"corrupt TGA rejected by real decoder without partial replacement");
    files[ammo]=Tga(24,24);InvalidateSelectedRow();PrepareSelection();BeginHudStyle(0,0);Check(HudStyleWeaponVisible(),"downloaded weapon row recovers");
    state.id=999;PrepareSelection();BeginHudStyle(0,0);Check(!HudStyleWeaponVisible()&&!weaponImage.id,"switch to unregistered weapon retires prior art");state.id=400;PrepareSelection();
    state.kind=csol::hud_style::AmmoKind::Total;state.total=30;Draw();Check(DigitsAt(681).ends_with("30"),"total-ammo weapon keeps all thirty throws");
    for(const auto& q:capture.quads)if(q.y==681&&q.x>1000&&(q.texture==digits[3].id||q.texture==digits[0].id))
        Check(q.color==std::array<int,4>{255,255,255,255},"ordinary total-ammo weapon keeps white digits");
    state.specialAmmo=true;Draw();
    for(const auto& q:capture.quads)if(q.y==681&&q.x>1000&&(q.texture==digits[3].id||q.texture==digits[0].id))
        Check(q.color==std::array<int,4>{114,197,255,255},"explicit special ammunition uses original blue digits");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==ammoImage.id&&q.additive&&q.color==std::array<int,4>{114,197,255,255};}),"special primary icon preserves original blue color");
    state.total=0;Draw();
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==digits[0].id&&q.y==681&&q.x>1000&&q.right-q.x==17&&q.bottom-q.y==22;}),"empty special amount uses the same proven digit atlas and size");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==ammoImage.id&&q.color==std::array<int,4>{114,197,255,255};}),"empty special icon remains blue independently of numeric tint");
    state.total=30;
    state.specialAmmo=false;
    Draw();Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==ammoImage.id&&q.color==std::array<int,4>{255,255,255,255};}),"ordinary primary icon resets tint to white after special ammunition");
    state.infinite=true;Draw();Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==art[unsigned(Art::Infinite)].id&&q.additive;}),"authoritative infinite state uses original infinity SPR");state.infinite=false;
    state.kind=csol::hud_style::AmmoKind::None;Draw();Check(DigitsAt(681)=="65536200","melee has title and art without phantom ammo");
    Reset("ResetHUD",0,nullptr);BeginHudStyle(0,0);Check(!vitals.healthKnown&&!vitals.armorKnown&&!HudStyleVitalsVisible()&&!money.known&&!HudStyleMoneyVisible()&&moneyChangedAt==0,"round reset forgets prior life and account state");
    Seed();SendMoney(1000);newSession=true;hp=Bytes(2000,4);Health("Health",4,hp.data());Check(vitals.health==2000&&vitals.healthKnown&&!vitals.armorKnown&&!money.known,"new session retires old armor and account before first HP");
    SendMoney(2000);newSession=true;SendMoney(7500);Check(money.amount==7500&&money.delta==0&&!money.changed&&!money.trackChange,"new session observed first by Money cannot invent previous-session income");
    int authoritativeHealth=70000,authoritativeArmor=1024;nativeHealth=&authoritativeHealth;nativeArmor=&authoritativeArmor;
    HudStyleFrame();Check(vitals.health==70000&&vitals.armor==1024,"matched native globals seed missed signon values without byte clamp");
    nativeHealth=nativeArmor=nullptr;Init("InitHUD",0,nullptr);Check(!vitals.healthKnown&&!vitals.armorKnown&&!money.known&&moneyChangedAt==0,"InitHUD life and account state cleared");
    SendMoney(7500);clientTime=140;SendMoney(8000);const auto videoMoneyTime=moneyChangedAt;
    HudStyleVideoInit();Check(!loaded&&!moneyLoaded&&capture.live.empty()&&!HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&!HudStyleMoneyVisible(),"video reload deletes every owned texture and releases old HUD");
    Check(money.amount==8000&&money.delta==500&&money.trackChange&&moneyChangedAt==videoMoneyTime&&moneyFadeActive,"video reload preserves authoritative account and original timer");
    message_chain::installed.clear();messages.clear();
    for(const char* name:{"Health","Battery","Money","ResetHUD","InitHUD"})messages[name].function=Previous;
    const auto beforeReload=schemeManager.loads;InitializeHudStyle();
    Check(installs==3&&messages["Money"].function==MoneyMessage&&messages["Health"].function==Previous&&messages["Battery"].function==Previous,"Money observes signon immediately while Health and Battery defer past native HUD init");
    auto moneyPacket=MoneyPacket(7500);const auto moneyCalls=forwards;
    Check(messages["Money"].function("Money",5,moneyPacket.data())==73&&forwards==moneyCalls+1&&money.amount==7500,"immediate engine Money chain receives signon balance exactly once");
    HudStyleFrame();Check(installs==5&&messages["Health"].function==Health&&messages["Battery"].function==Armor,"first frame installs only two deferred observers");
    Check(schemeManager.loads==beforeReload+1&&titleFont==99&&capture.fontCreations==1,"video reload reacquires original scheme once and reuses its fixed font recipe");
    Check(vitals.health==1900&&vitals.armor==400,"deferred install seeds exact wide native globals");
    auto packet=Bytes(1800,4);const auto calls=forwards;
    Check(messages["Health"].function("Health",4,packet.data())==73&&forwards==calls+1&&vitals.health==1800,"engine head executes observer then preserved original handler");
    auto heads=CaptureChainedUserMessageHeads();RefreshChainedUserMessageHeads(heads);InitializeHudStyle();HudStyleFrame();
    Check(installs==5,"video/init without downstream replacement does not wrap the same handlers twice");
    heads=CaptureChainedUserMessageHeads();messages["Health"].function=NativeReplacement;RefreshChainedUserMessageHeads(heads);
    InitializeHudStyle();HudStyleFrame();
    Check(installs==6&&previousHealth==NativeReplacement,"observed stock HUD re-registration replaces only changed chain");
    Check(messages["Health"].function("Health",4,packet.data())==91,"new native handler remains callable without self recursion");
    heads=CaptureChainedUserMessageHeads();messages["Money"].function=NativeReplacement;RefreshChainedUserMessageHeads(heads);
    InitializeHudStyle();HudStyleFrame();Check(installs==7&&previousMoney==NativeReplacement,"Money re-registration replaces only its changed native chain");
    const auto replacedCalls=forwards;
    Check(messages["Money"].function("Money",5,moneyPacket.data())==91&&forwards==replacedCalls+1&&money.amount==7500,"replaced native Money handler remains reachable exactly once");
    HudStyleVideoInit();Check(capture.live.empty(),"reinitialized resources also released");
    // Exercise the actual consumer's failed glyph setup and its existing
    // wall-clock retry gate. Do not reset the deadline to manufacture recovery.
    previousHealth=previousArmor=Previous;
    Seed();state.id=400;state.kind=csol::hud_style::AmmoKind::Magazine;
    titleFonts.Invalidate();capture.glyphsReady=false;
    const auto beforeFailedGlyphs=capture.fontCreations;
    HudStyleFrame();Draw();
    Check(!loaded&&!titleFont&&!HudStyleVitalsVisible()&&!HudStyleWeaponVisible()&&HudStyleMoneyVisible(),
        "failed AddGlyphSetToFont preserves native base HUD and independent money");
    Check(capture.fontCreations==beforeFailedGlyphs+1&&capture.texts.empty(),
        "failed glyph recipe never draws a stale title");
    const auto failedReads=reads,failedGlyphs=capture.fontCreations;
    capture.glyphsReady=true;HudStyleFrame();Draw();
    Check(!loaded&&reads==failedReads&&capture.fontCreations==failedGlyphs,
        "glyph failure respects the existing retry deadline after readiness changes");
    const auto now=GetTickCount64();
    Check(retryAfter>=now&&retryAfter-now<=5000,"glyph retry uses the existing bounded five-second gate");
    if(retryAfter>=now&&retryAfter-now<=5000)Sleep(DWORD(retryAfter-now+1));
    HudStyleFrame();Draw();
    Check(loaded&&titleFont==99&&HudStyleVitalsVisible()&&HudStyleWeaponVisible()&&
        capture.fontCreations==failedGlyphs+1&&!capture.texts.empty(),
        "actual timed retry discards only the failed glyph recipe and restores the HUD");
    HudStyleVideoInit();
    if(argc==2)
    {
        files.clear();const std::filesystem::path root=argv[1];
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file())
        {
            std::ifstream input(entry.path(),std::ios::binary);
            const std::vector<char> value(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>{});
            Put(entry.path().lexically_relative(root).generic_string(),std::string(value.begin(),value.end()));
        }
        const auto& catalogBytes=files.at(csol::hud_style::CatalogPath);
        const auto expectedCatalog=csol::hud_style::ParseCatalog({reinterpret_cast<const char*>(catalogBytes.data()),catalogBytes.size()});
        // The synthetic-resource suite uses a controlled already-registered
        // owner. This branch instead registers the supplied real TTF through
        // production RegisterResourceFont and Windows GDI.
        privateFont=nullptr;
        Prepare();PrepareMoney();Check(loaded&&moneyLoaded&&catalog.weapons.size()==expectedCatalog.weapons.size()&&catalog.portraits.size()==expectedCatalog.portraits.size(),"real export decoded with actual production parser and money loader");
#if CSOL_RESOURCE_REFRESH
        const auto registeredFonts=SnapshotResourceFonts();
        Check(registeredFonts.size()==1&&registeredFonts[0].owner==&privateFont&&
            registeredFonts[0].handle==privateFont&&privateFont&&
            registeredFonts[0].resource==csol::hud_style::FontPath&&
            registeredFonts[0].family=="Sarasa UI SC"&&registeredFonts[0].bytes&&
            *registeredFonts[0].bytes==files.at(csol::hud_style::FontPath),
            "actual HUD TTF owner publishes Sarasa family and exact bytes to ResourceRefresh snapshot");
#endif
        previousHealth=previousArmor=previousMoney=Previous;
        nativeHealth=nativeArmor=nullptr;Seed();SendMoney(32000,0);state.kind=csol::hud_style::AmmoKind::Magazine;
        for(const auto& [name,row]:catalog.weapons)
        {
            definition.name=name;PrepareSelection();Draw();
            Check(weaponImage.id&&HudStyleWeaponVisible()==!row.ammo.empty(),"each exported weapon uses actual loader and replacement gate");
            Check(HudStyleMoneyVisible()&&MoneyAt(200)=="32000","every real weapon selection keeps independent money account");
            if(row.ammo.empty()){state.kind=csol::hud_style::AmmoKind::None;Draw();Check(HudStyleWeaponVisible(),"melee without ammo retains title and silhouette");state.kind=csol::hud_style::AmmoKind::Magazine;}
            const auto saved=state;
            if(name=="weapon_k3")for(const auto values:{std::array<int,2>{100,2000},{100,1000},{0,2000}})
            {
                state.kind=csol::hud_style::AmmoKind::Magazine;state.clip=values[0];state.reserve=values[1];state.specialAmmo=false;
                Draw();Check(WeaponDigits()==std::to_string(values[0])+std::to_string(values[1]),"real exported K3 art preserves magazine and wide reserve digits");
            }
            if(name=="weapon_cannon"||name=="weapon_tknifeex2")
            {
                state.kind=csol::hud_style::AmmoKind::Total;state.total=name=="weapon_cannon"?20:30;state.specialAmmo=true;Draw();
                Check(WeaponDigits()==std::to_string(state.total),"real exported special weapon preserves complete total ammunition");
                Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
                    {return q.texture==ammoImage.id&&q.color==std::array<int,4>{114,197,255,255};}),"real exported special icon stays blue");
            }
            state.kind=csol::hud_style::AmmoKind::None;selectionItems={definition.id};selectionPending=definition.id;
            preparedSelection=0;Draw();
            Check(selectionHudOwner==row.selectedKnown,"every real selected branch requires explicit catalog evidence");
            if(row.selectedKnown&&!row.selected.empty())
                Check(selectionCards.at(definition.id).selectedImage.id>0,"every exported real weapon_s decodes through production preparation");
            selectionPending=0;Draw();state=saved;
        }
        for(const auto& [id,path]:catalog.portraits){character.id=id;PrepareSelection();Check(portrait.id>0,"every generated portrait decodes through real TGA loader");}
        for(int id:{39,40}){character.id=id;PrepareSelection();BeginHudStyle(0,0);Check(!portrait.id&&HudStyleVitalsVisible(),"missing original portrait never substitutes wrong person or removes vitals");}
        HudStyleVideoInit();Check(capture.live.empty(),"real catalog texture lifecycle has no outstanding ownership");
#if CSOL_RESOURCE_REFRESH
        const auto videoFonts=SnapshotResourceFonts();
        Check(videoFonts.size()==1&&videoFonts[0].handle==privateFont,
            "VideoInit retains the tracked private font until owner shutdown");
#endif
        // This fixture calls Paint directly and never registered a UI callback.
        extension=nullptr;ShutdownHudStyle();
#if CSOL_RESOURCE_REFRESH
        Check(SnapshotResourceFonts().empty()&&!privateFont,
            "actual owner shutdown retires the GDI registration from ResourceRefresh");
#endif
    }
    privateFont=nullptr;extension=nullptr;surface=nullptr;schemes=nullptr;
    std::cout<<"hud_style_client checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
