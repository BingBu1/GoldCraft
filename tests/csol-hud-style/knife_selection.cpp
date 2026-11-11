// SPDX-License-Identifier: GPL-3.0-or-later
// Real exporter output -> production catalog/codec/HUD and slot3 inventory path.
// The engine input transport and surface remain controlled; no physical input/GPU.
#define CSOL_INVENTORY_TEST
#define main UnusedCompleteHudRegression
#include "client.cpp"
#undef main
#include <sstream>

namespace csol::client {
cl_exportfuncs_t downstream{};
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&){}
void ReceiveExtendedWeaponState(const WeaponCurrent&){}
void AddWeaponPickup(int){}
void ClearNativeWeaponSelection(){}
bool TryActivateScenarioAbility(){return false;}
bool TryScenarioHealingSlot(int){return false;}
void* Resolve(const char*,mh_gamesymbol_kind_t){return nullptr;}
}
namespace {
std::map<std::string,xcommand_t> knifeCommands;
unsigned legacyStrings{},nativeSlots{};
void NativeSlot(){++nativeSlots;}
}
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;using namespace fixture;
    if(argc!=2){std::cerr<<"expected real exporter output directory\n";return 2;}
    Configure();Seed();files.clear();
    const std::filesystem::path root=argv[1];
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file())
    {
        std::ifstream stream(entry.path(),std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),{});
        auto& destination=files[entry.path().lexically_relative(root).generic_string()];
        destination.resize(bytes.size());std::memcpy(destination.data(),bytes.data(),bytes.size());
    }
    apiData.HookCmd=[](const char* name,xcommand_t callback)->xcommand_t
    {knifeCommands[name]=callback;return NativeSlot;};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook callback)->int
    {messages[name].function=callback;return 1;};
    engine.pfnAddCommand=[](const char* name,xcommand_t callback)->int{knifeCommands[name]=callback;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{return &option;};
    engine.Cvar_SetValue=[](const char*,float){};
    engine.pfnGetCvarFloat=[](const char* name)->float{return !std::strcmp(name,"hud_fastswitch")?0.f:hudDraw;};
    engine.pfnServerCmd=[](const char*)->int{return 1;};
    engine.pfnPlaySoundByName=[](const char*,float){};
    engine.pfnGetScreenInfo=[](SCREENINFO* screen)->int{screen->iWidth=1280;screen->iHeight=720;return 1;};
    engine.pfnSPR_GetList=[](char*,int* count)->client_sprite_t*{*count=0;return nullptr;};
    engine.pfnDrawString=[](int,int,const char*,int,int,int)->int{++legacyStrings;return 0;};
    engine.pfnFillRGBA=[](int,int,int,int,int,int,int,int){};
    InitializeWeaponInventory();ResetWeaponInventory(true);
    auto send=[](const char* name,const std::vector<std::byte>& bytes)
    {messages.at(name).function(name,int(bytes.size()),const_cast<std::byte*>(bytes.data()));};
    std::vector<unsigned char> stock{'w','e','a','p','o','n','_','k','n','i','f','e',0};
    for(int value:{255,255,255,255,2,0,29,0})stock.push_back(static_cast<unsigned char>(value));
    ObserveLegacyWeaponDefinition(int(stock.size()),stock.data());
    for(const auto& [id,position,name]:std::array{
        std::tuple{1022,1,"weapon_dragonsword"},std::tuple{1023,2,"weapon_skullaxe"}})
        send(WeaponDefinitionMessage,EncodeWeaponDefinition({id,2,position,-1,-1,-1,-1,-1,0,name}));
    for(int id:{29,1022,1023})Check(FindWeaponDefinition(id)!=nullptr,"real stock/extended message paths admit every owned knife definition");
    WeaponOwnership owned{};for(int id:{29,1022,1023})owned[id]=true;
    for(const auto& part:EncodeWeaponInventory(owned,1))send(WeaponInventoryMessage,part);
    send(WeaponCurrentMessage,EncodeWeaponCurrent({1022,1,-1,0,0}));
    state={};state.id=1022;state.slot=2;state.kind=hud_style::AmmoKind::None;
    Prepare();cardsRetryAfter=0;PrepareSelection();Draw();
    Check(loaded&&HudStyleWeaponVisible(),"real export keeps DragonSword current card ready");
    for(const char* name:{"weapon_dragonsword","weapon_skullaxe"})
    {
        const auto& entry=catalog.weapons.at(name);
        Check(entry.selectedKnown&&!entry.selected.empty(),"original knife setter requires exported selected metadata");
        Check(entry.selected==entry.weapon,"original knife setter supplies the same card to normal and selected consumers");
    }
    auto drawSelection=[](int expected,bool modern)
    {
        const auto pending=WeaponSelectionForHud();
        Check(pending.selected==expected&&pending.items.size()==3,"actual repeated slot3 cycles the three owned same-slot weapons");
        cardsRetryAfter=0;PrepareSelection();Draw();
        const auto before=legacyStrings;DrawWeaponSelection(0,0);
        Check(selectionReady==modern&&HudStyleWeaponVisible(),"pending-list readiness preserves the independent current card");
        Check((legacyStrings==before)==modern,"complete modern selection suppresses yellow fallback; unknown schema retains it");
        if(modern)
        {
            Check(selectionRows.size()==4&&selectionRows[0].equipped&&selectionRows[0].id==1022,
                "three pending rows retain the real equipped off-row");
            const auto row=std::find_if(selectionRows.begin(),selectionRows.end(),[&](const auto& item){return item.selected&&item.id==expected;});
            Check(row!=selectionRows.end()&&row->expanded&&row->card->selectedImage.id,
                "actual selected knife row uses prepared selected texture");
            if(row!=selectionRows.end())Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& quad)
                {return quad.texture==row->card->selectedImage.id&&quad.additive;}),"selected card reaches the real additive HUD consumer");
        }
    };
    for(int expected:{29,1022,1023,29,1022,1023,29}){knifeCommands.at("slot3")();drawSelection(expected,true);}
    Check(!nativeSlots,"owned extended inventory routes slot3 without forwarding native slot command");
    auto& unknown=catalog.weapons.at("weapon_dragonsword");
    const auto selectedPath=unknown.selected;unknown.selectedKnown=false;unknown.selected.clear();
    Delete(selectionCards.at(1022).selectedImage);selectionCards.at(1022).selectedRetryAfter=0;
    knifeCommands.at("slot3")();preparedSelection=0;drawSelection(1022,false);
    unknown.selected=selectedPath;unknown.selectedKnown=true;
    cardsRetryAfter=0;PrepareSelection();preparedSelection=0;drawSelection(1022,true);
    ResetWeaponInventory(true);ResetHudStyle();HudStyleVideoInit();
    Check(capture.live.empty(),"knife selection textures retire exactly once on reset/video lifecycle");
    privateFont=nullptr;extension=nullptr;surface=nullptr;schemes=nullptr;
    std::cout<<"knife_selection checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
