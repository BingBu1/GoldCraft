// SPDX-License-Identifier: GPL-3.0-or-later
// The entire production consumer is compiled with the real client SDK. Only
// external message/resource/input exits are supplied. No VGUI/GL pixel claim.
#include "native/csol/client/scenario_respawn_ui.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>

namespace fixture
{
unsigned checks{},failures{},forwards{},syncs{},reads{};
float now=100;
bool newSession{},buy{},character{},textMenu{};
std::filesystem::path common;
std::map<std::string,pfnUserMsgHook> hooks;
std::vector<std::string> sent;
std::vector<std::string> sounds;
std::string playerName="玩家%d%s";
unsigned playerRequests{};
std::array<int,3> tint;
struct Draw { int sprite,x,y;wrect_t rect;std::array<int,3> tint; };
std::vector<Draw> draw;
int currentSprite{};
unsigned noticeChannel{}, noticeCount{};
std::wstring noticeText;
std::array<std::uint8_t,3> noticeColor{};
void Check(bool ok,const char* label,std::source_location at=std::source_location::current())
{++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<" line="<<at.line()<<'\n';}}
int Previous(const char*,int,void*){++forwards;return 73;}
void Log(const char*,...){}
std::vector<std::byte> Bytes(std::initializer_list<unsigned> values)
{std::vector<std::byte>b;for(auto v:values)b.push_back(std::byte(v));return b;}
int Send(const char* name,std::vector<std::byte>b)
{return hooks.at(name)(name,int(b.size()),b.data());}
void String(std::vector<std::byte>&b,std::string_view s)
{for(auto c:s)b.push_back(std::byte(c));b.push_back(std::byte{});}
std::vector<std::byte> Center(std::string_view token,std::string_view param={})
{auto b=Bytes({4});String(b,token);if(!param.empty())String(b,param);return b;}
std::vector<std::byte> Item(unsigned index,float cooldown,int count)
{
    auto b=Bytes({27,4,index});auto bits=std::bit_cast<std::uint32_t>(cooldown);
    for(unsigned shift:{0u,8u,16u,24u})b.push_back(std::byte(bits>>shift));
    b.push_back(std::byte(unsigned(count)));b.push_back(std::byte(unsigned(count)>>8));return b;
}
std::vector<std::byte> Budget(std::int32_t remaining,std::int32_t initial)
{
    auto b=Bytes({27,29});
    for(auto value:{remaining,initial})
        for(unsigned shift:{0u,8u,16u,24u})b.push_back(std::byte(std::bit_cast<std::uint32_t>(value)>>shift));
    return b;
}
}
#ifndef CSOL_INTERFACE_TEST
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
namespace csol
{
Image DecodeTGA(std::span<const std::byte>)
{throw std::runtime_error("image decode must not run without surface");}
}
#endif
namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
ref_params_t view{};
bool BuyMenuVisible(){return fixture::buy;}
bool CharacterMenuVisible(){return fixture::character;}
bool ShowScenarioRoundHudText(unsigned channel, std::wstring_view text, const std::array<std::uint8_t,3>& color)
{fixture::noticeChannel=channel;fixture::noticeText=text;fixture::noticeColor=color;++fixture::noticeCount;return true;}
#ifndef CSOL_RESPAWN_ROUTE_TEST
bool WeaponTextMenuVisible(){return fixture::textMenu;}
#endif
void SynchronizeSession()
{++fixture::syncs;if(fixture::newSession){fixture::newSession=false;ResetScenarioRespawnUi();}}
std::vector<std::byte> ReadResource(const char* path)
{
    ++fixture::reads;std::ifstream f(fixture::common/path,std::ios::binary);
    if(!f)throw std::runtime_error("actual unpacked resource missing");
    std::vector<std::byte>b;for(char c;f.get(c);)b.push_back(std::byte(static_cast<unsigned char>(c)));return b;
}
}
#ifndef CSOL_INTERFACE_TEST
int wmain(int argc,wchar_t**argv)
{
    try
    {
        using namespace fixture;using namespace csol::client;
        if(argc!=2)throw std::runtime_error("expected actual unpacked common directory");common=argv[1];
        metahook_api_t storage{};api=&storage;
        storage.GetEngineFactory=[]()->CreateInterfaceFn{return nullptr;};
        storage.GetPluginModuleHandleByBaseFileName=[](const char*)->HINTERFACEMODULE{return nullptr;};
        storage.HookUserMsg=[](const char*name,pfnUserMsgHook hook)->pfnUserMsgHook
        {const auto prior=hooks.contains(name)?hooks.at(name):Previous;hooks[name]=hook;return prior;};
        engine.GetClientTime=[]{return now;};
        engine.pfnGetCvarFloat=[](const char*name){return std::string_view(name)=="scr_centertime"?2.f:1.f;};
        engine.Con_DPrintf=Log;
        engine.pfnServerCmd=[](const char*command){sent.emplace_back(command);return 1;};
        engine.pfnPlaySoundByNameAtPitch=[](const char* sound,float volume,int pitch)
        {Check(volume==1&&pitch==100,"retry sound original volume and pitch");sounds.emplace_back(sound);};
        engine.pfnGetPlayerInfo=[](int index,hud_player_info_t* player)
        {Check(index==32,"announcement requests broadcast player index");player->name=playerName.data();++playerRequests;};
        engine.pfnSPR_Set=[](int sprite,int r,int g,int b){currentSprite=sprite;tint={r,g,b};};
        engine.pfnSPR_DrawAdditive=[](int,int x,int y,const wrect_t*rect){draw.push_back({currentSprite,x,y,*rect,tint});};
        view.health=100;
        InitializeScenarioRespawnUi();InitializeScenarioRespawnUi();
        Check(hooks.size()==9,"all production message hooks installed once");
        Check(!TryScenarioHealingSlot(5),"native slot5 untouched before scenario");
        Check(Send("HudIcon",Bytes({1,128}))==73&&!state.Active()&&state.Mask()==1,"early visibility mask retained and forwarded");
        Check(Send("MPToCL",Item(0,60,11))==73&&state.Active()&&state.Items()[0].cooldownEnd==160,
            "production receiver consumes float32 packet and keeps message chain");
        Check(forwards==2,"repeat initialize does not form forwarding cycle");
        auto count=accepted;auto oldForwards=forwards;
        Check(Send("MPToCL",Bytes({27,4,0,0,0,11,0}))==1&&accepted==count&&forwards==oldForwards,
            "bad short-coordinate packet rejected without corrupting state");
        Check(Send("MPToCL",Bytes({27,7,193,0}))==73,"existing dialogue opcode forwarded");
        Check(TryScenarioHealingSlot(5)&&sent.size()==1&&sent.back()=="useitem \"zbs3\"\n","slot5 requests original 30 percent item");
        Check(state.Items()[0].count==11&&state.Items()[0].cooldownEnd==160,"input does not predict count or cooldown");
        Check(TryScenarioHealingSlot(6)&&sent.size()==1,"missing full-heal inventory sends nothing");
        Send("HudIcon",Bytes({3,0}));Send("MPToCL",Item(1,0,7));
        Check(TryScenarioHealingSlot(6)&&sent.size()==2&&sent.back()=="useitem \"zbs10\"\n","slot6 requests original full heal");
        for(int slot:{1,2,3,4,7,8,9,10,82})Check(!TryScenarioHealingSlot(slot),"unowned native slots and R untouched");
        const auto inputCount=sent.size();
        for(bool* gate:{&buy,&character,&textMenu,&gameUI,&console,&intermission})
        {*gate=true;Check(TryScenarioHealingSlot(5)&&sent.size()==inputCount,"menu or intermission blocks healing input");*gate=false;}
        hidden=4;TryScenarioHealingSlot(5);hidden=0;view.health=0;TryScenarioHealingSlot(5);view.health=100;
        view.spectator=1;TryScenarioHealingSlot(5);view.spectator=0;
        Check(sent.size()==inputCount,"hidden, dead and spectator states cannot use healing");
        Check(Send("TextMsg",Center("#Buy"))==73,"ordinary TextMsg forwarded");
        Check(Send("TextMsg",Center("#CSO_ZBS_AutoRespawnWait","30"))==1&&state.Center(now)->seconds==30,
            "known countdown consumed once");
        LoadLocalization();
        Check(localized&&reads==1&&centerText==L"复活剩余时间 (30秒)","actual Chinese resource formats authoritative countdown");
        Check(hintTexts[0]==L"使用复活道具即可立即复活 (按键：R)","actual original revival hint localized");
        Check(budgetTitle==L"回合重置可使用次数"&&budgetUnlimited==L"：无限制",
            "actual original retry budget title and unlimited resource tokens localized");
        Check(Send("MPToCL",Budget(70000,90000))==73&&state.Budget().remaining==70000&&
            state.Budget().initial==90000&&budgetText==L"70000/90000",
            "actual receiver retains both LONGs above signed16 range and forwards the message");
        Check(Send("MPToCL",Budget(0,0))==73&&budgetText==L"0/0","zero limit remains visible without granting retry");
        Check(Send("MPToCL",Budget(0,-1))==73&&budgetText==L"：无限制",
            "original unlimited label is selected by initial LONG");
        Check(Send("MPToCL",Budget(7,-2))==73&&budgetText.empty()&&!budgetTitle.empty(),
            "original minus2 initial clears count while retaining title");
        Check(Send("MPToCL",Budget(2147483647,2147483647))==73&&budgetText==L"2147483647/2147483647",
            "signed32 maximum is not narrowed or reformatted as unsigned16");
        Send("MPToCL",Budget(70000,90000));
        count=accepted;oldForwards=forwards;
        const auto budgetSounds=sounds.size();const auto budgetNotices=noticeCount;
        for(unsigned length=2;length<10;++length)
        {
            auto malformedBudget=Budget(1,2);malformedBudget.resize(length);
            Check(Send("MPToCL",malformedBudget)==1&&accepted==count&&forwards==oldForwards&&
                state.Budget().remaining==70000&&budgetText==L"70000/90000",
                "truncated or old SHORT budget cannot mutate actual consumer or reach downstream");
        }
        auto trailingBudget=Budget(1,2);trailingBudget.push_back(std::byte{});
        Check(Send("MPToCL",trailingBudget)==1&&accepted==count&&forwards==oldForwards,
            "budget trailing byte is rejected");
        for(const auto& invalidBudget:{Budget(-3,2),Budget(1,-3),Budget(INT32_MIN,2),Budget(1,INT32_MIN)})
            Check(Send("MPToCL",invalidBudget)==1&&accepted==count&&forwards==oldForwards&&
                state.Budget().remaining==70000&&budgetText==L"70000/90000","unknown budget sentinel preserves prior state");
        Send("MPToCL",Bytes({27,3,2,0,0,0,0,0}));
        Check(state.Budget().remaining==70000&&state.Budget().initial==90000&&budgetText==L"70000/90000"&&
            sounds.size()==budgetSounds&&noticeCount==budgetNotices,
            "ordinary round notification neither refills budget nor plays retry sound or announcement");
        Check(Send("ZBSVer2",Bytes({0x1c,45,0,0,0}))==1&&noticeCount==1&&noticeChannel==6&&
            noticeText==L"道具冷却时间剩余45秒，无法立即复活"&&noticeColor==std::array<std::uint8_t,3>{255,195,35},
            "original R cooldown notice uses localized channel6 and original RGB");
        Check(Send("ZBSVer2",Bytes({0x1c,255,255,255,255}))==1&&noticeCount==1,
            "invalid remaining time does not overwrite cooldown notice");
        Check(Send("ZBSVer2",Bytes({0x24,1}))==73&&noticeCount==1,"unrelated skill opcode retains original chain");
        for(int value=10;value>=0;--value)
        {
            Check(Send("MPToCL",Bytes({27,15,unsigned(value)}))==73&&state.RetryCountdown()==value,
                "production retry consumer retains each broadcast number and forwards other observers");
        }
        Check(sounds.size()==10&&sounds.front()=="vox/Ten.wav"&&sounds.back()=="vox/One.wav",
            "ten-to-zero speaks ten original files, zero is silent");
        Check(Send("MPToCL",Bytes({27,15,255}))==73&&state.RetryCountdown()==-1&&sounds.back()=="zombi/TD_Heal.wav",
            "signed minus one hides countdown and plays original retry acceptance sound");
        count=accepted;const auto soundCount=sounds.size();
        Check(Send("MPToCL",Bytes({27,15,254}))==1&&accepted==count&&sounds.size()==soundCount,
            "malformed signed countdown has no audio or state side effects");
        Check(Send("MPToCL",Bytes({27,16,32,2}))==73&&noticeCount==2&&noticeChannel==1&&playerRequests==1&&
            noticeText==L"[玩家%d%s]玩家使用了回合重置。3秒后2关将重新开始。"&&noticeColor==std::array<std::uint8_t,3>{166,241,255},
            "original retry localized player notice uses channel1 and blue RGB safely");
        Check(Send("MPToCL",Bytes({27,16,33,2}))==1&&noticeCount==2&&playerRequests==1,
            "invalid player index never reaches engine player lookup");
        Check(Send("TextMsg",Center("#CSO_ZBS_AutoRespawnWait","-1"))==73&&state.Center(now)->seconds==30,
            "malformed countdown does not replace valid prior state");
        auto hint=Bytes({});String(hint,"#CSO_ZBS_UseRevivalItem");hint.push_back(std::byte{1});
        Check(Send("HudTextPro",hint)==1&&state.Hint(now)==0,"original personal revival hint route consumed");
        auto retry=Bytes({});String(retry,"#CSO_ZBS_UseRetryItem");retry.push_back(std::byte{1});retry.push_back(std::byte{});
        Check(Send("HudTextArgs",retry)==1&&state.Hint(now)==1,"round retry hint uses separate HudTextArgs route");
        Check(Send("MPToCL",Bytes({25,8}))==73&&state.Outcome()==0,"server RoundClear becomes visible outcome state");
        now=100.25f;Check(state.OutcomeAlpha(now)==127,"real consumer uses client time for fade");
        Send("TextMsg",Center("恭喜！！您已经成功通关！"));
        Check(centerText==L"恭喜！！您已经成功通关！","existing literal all-round server notice becomes Chinese HUD");
        itemSprite=17;Send("HudIcon",Bytes({1,0}));now=130;view.health=0;view.spectator=1;
        DrawScenarioRespawnUi(now,0);
        Check(draw.size()==5,"dead player still sees item icon, cooldown, multiplier and two digits");
        Check(draw[0].sprite==17&&draw[0].x==10&&draw[0].y==273&&draw[0].rect.left==1&&
            draw[0].rect.right==69&&draw[0].rect.top==1&&draw[0].rect.bottom==28&&draw[0].tint==std::array<int,3>{39,54,8},
            "production native sprite draw uses original fixed pixel crop and dim tint");
        Check(draw[1].y==286&&draw[1].rect.top==14&&draw[1].tint==std::array<int,3>{91,126,18},
            "production cooldown crops top and keeps bottom bright");
        Check(draw[2].x==79&&draw[3].x==92&&draw[4].x==105&&draw[3].rect.left==27,
            "original multiplier and decimal digit atlas placement");
        view.health=100;view.spectator=0;
        Send("ResetHUD",Bytes({0}));
        Check(state.Active()&&state.Mask()==1&&state.Items()[0].count==11&&state.Items()[0].cooldownEnd==160,
            "real ResetHUD preserves authoritative consumables and timer");
        Check(state.Outcome()<0&&state.Hint(now)<0&&!state.Center(now)&&centerText.empty(),"real ResetHUD clears old life presentation");
        Check(state.Budget().remaining==70000&&state.Budget().initial==90000&&budgetText==L"70000/90000",
            "real ResetHUD preserves authoritative challenge budget");
        ScenarioRespawnUiVideoInit();Check(itemSprite==0&&state.Items()[0].count==11&&localized,"video reset retires graphics but keeps authoritative data");
        Check(state.Budget().remaining==70000&&budgetText==L"70000/90000","video reset preserves retry budget and localized count");
        ScenarioRespawnUiFrame();Check(reads==1,"no-surface frame does not attempt graphics IO");
        Send("MPToCL",Bytes({25,9}));DrawScenarioRespawnUi(now,1);
        Check(requested&&intermission,"round outcome survives intermission while input is blocked");
        Send("InitHUD",Bytes({0}));
        Check(!state.Active()&&!state.Mask()&&!localized&&!TryScenarioHealingSlot(5),"same-map reconnect fully resets and releases slots");
        Check(state.Budget().initial==-2&&state.Budget().remaining==-2&&budgetText.empty()&&budgetTitle.empty(),
            "InitHUD clears previous session budget and localization");
        Send("MPToCL",Budget(1,3));
        Check(state.Budget().remaining==1&&state.Budget().initial==3&&budgetText==L"1/3",
            "late join snapshot installs remaining count without inferring a refill");
        Send("MPToCL",Item(1,0,7));newSession=true;
        Send("HudIcon",Bytes({2,0}));
        Check(!state.Active()&&state.Mask()==2&&state.Items()[1].count==-1,"session is reset before accepting new early mask");
        Check(state.Budget().initial==-2&&budgetText.empty(),"new-session synchronization retires old retry snapshot before next message");
        ShutdownScenarioRespawnUi();Check(!state.Active()&&!surface&&!extension&&!requested,"shutdown retires UI state");
        std::cout<<"scenario_respawn_client checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }
    catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
}
#endif
