// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/client/scenario/core/scenario_respawn_ui.hpp"
#include <iostream>
#include <limits>
#include <source_location>
#include <vector>

namespace hud = csol::scenario_respawn_ui;
namespace test
{
unsigned checks{}, failures{};
void Check(bool ok, const char* label, std::source_location at = std::source_location::current())
{ ++checks; if (!ok) { ++failures; std::cerr << "FAIL " << label << " line=" << at.line() << '\n'; } }
std::vector<std::byte> Bytes(std::initializer_list<unsigned> values)
{ std::vector<std::byte> b; for (auto v : values) b.push_back(std::byte(v)); return b; }
void String(std::vector<std::byte>& b, std::string_view text)
{ for (auto c : text) b.push_back(std::byte(c)); b.push_back(std::byte{}); }
std::vector<std::byte> Text(std::string_view token, std::string_view argument = {}, bool original = false)
{
    auto b = original ? Bytes({4, 0, 0, 0}) : Bytes({4}); String(b, token);
    if (!argument.empty()) String(b, argument);
    return b;
}
std::vector<std::byte> Item(unsigned index, float cooldown, int count)
{
    auto b = Bytes({27,4,index}); const auto bits = std::bit_cast<std::uint32_t>(cooldown);
    for (unsigned shift : {0u,8u,16u,24u}) b.push_back(std::byte(bits >> shift));
    b.push_back(std::byte(unsigned(count))); b.push_back(std::byte(unsigned(count) >> 8)); return b;
}
void Protocol()
{
    const hud::Message sentinel{hud::Kind::Outcome, 1, 99, 2.5f};
    const auto unchanged = [&](const auto& m)
    { return m.kind == sentinel.kind && m.index == 1 && m.count == 99 && m.cooldown == 2.5f; };
    for (unsigned index = 0; index < 4; ++index) for (int count : {-32768,-1,0,1,9999,32767})
        for (float duration : {0.f,.5f,60.f,3600.25f})
        {
            auto b = Item(index,duration,count); auto m = sentinel;
            Check(hud::DecodeMP(b,m) == hud::ParseResult::Accepted, "original float32 item packet accepted");
            Check(m.kind == hud::Kind::Consumable && m.index == index && m.count == count && m.cooldown == duration,
                "signed quantity and float32 seconds preserved");
            for (unsigned length = 2; length < 12; ++length)
            {
                if (length == b.size()) continue;
                auto malformed = b; malformed.resize(length); m = sentinel;
                Check(hud::DecodeMP(malformed,m) == hud::ParseResult::Malformed && unchanged(m),
                    "truncation and trailing bytes leave output unchanged");
            }
        }
    for (float value : {-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    { auto m = sentinel; Check(hud::DecodeMP(Item(0,value,1),m) == hud::ParseResult::Malformed && unchanged(m), "invalid cooldown rejected transactionally"); }
    auto m = sentinel;
    Check(hud::DecodeMP(Item(4,1,1),m) == hud::ParseResult::Malformed && unchanged(m), "item index bounded");
    Check(hud::DecodeMP(Bytes({27,4,0,0,0,3,0}),m) == hud::ParseResult::Malformed, "stock short WRITE_COORD is not original float32");
    for (unsigned value : {8u,9u})
    { Check(hud::DecodeMP(Bytes({25,value}),m) == hud::ParseResult::Accepted && m.index == value-8 && m.kind == hud::Kind::Outcome, "both original outcome messages"); }
    for (auto b : {Bytes({25,7}),Bytes({27,7,1,0}),Bytes({27,25,0,0}),Bytes({28,4,0})})
    { m=sentinel; Check(hud::DecodeMP(b,m)==hud::ParseResult::Unrelated && unchanged(m), "neighboring original opcodes untouched"); }
    for (bool original : {false,true})
    {
        hud::CenterMessage center{99,77};
        Check(hud::DecodeText(Text("#CSO_ZBS_AutoRespawnWait","45",original),center)==hud::ParseResult::Accepted &&
            center.token==0 && center.seconds==45, "stock and original center wire decode");
        Check(hud::DecodeText(Text("#CSO_ZBS_RespawnWait","0",original),center)==hud::ParseResult::Accepted &&
            center.token==2 && center.seconds==0, "server zero accepted without local revival");
        for (auto value : {"-1","+2","3junk","2147483648"})
        {
            center={99,77}; Check(hud::DecodeText(Text("#CSO_ZBS_AutoRespawnWait",value,original),center)==hud::ParseResult::Malformed &&
                center.token==99 && center.seconds==77, "invalid countdown preserves output");
        }
        Check(hud::DecodeText(Text("#CSO_CongAllRoundClear",{},original),center)==hud::ParseResult::Accepted && center.token==4,
            "localized all-round completion accepted");
        Check(hud::DecodeText(Text("#Buy",{},original),center)==hud::ParseResult::Unrelated, "ordinary center message not intercepted");
    }
    hud::CenterMessage center{};
    Check(hud::DecodeText(Text("恭喜！！您已经成功通关！"),center)==hud::ParseResult::Accepted && center.token==4,
        "current server raw Chinese completion retains localization");
    for (unsigned token=0;token<hud::HintTokens.size();++token) for (bool args : {false,true})
    {
        auto b=Bytes({});String(b,hud::HintTokens[token]);b.push_back(std::byte{1});if(args)b.push_back(std::byte{0});
        unsigned decoded=99;
        Check(hud::DecodeHint(b,args,decoded)==hud::ParseResult::Accepted && decoded==token,"actual HudTextPro/Args R prompts");
        b.pop_back();decoded=99;
        Check(hud::DecodeHint(b,args,decoded)==hud::ParseResult::Malformed && decoded==99,"truncated hint not applied");
    }
}
void Lifecycle()
{
    hud::State s;
    Check(s.SetMask(Bytes({15,128})) && !s.Active() && s.Mask()==15,"mask can precede activation without claiming ordinary HUD");
    Check(s.HealingCommand(5)==nullptr,"ordinary server owns slot5");
    Check(s.Apply({hud::Kind::Round},100),"round activates");
    Check(s.Apply({hud::Kind::Consumable,0,12,60},100),"countdown starts on authoritative update");
    Check(s.CooldownRows(0,100)==27 && s.CooldownRows(0,130)==13 && s.CooldownRows(0,160)==0,
        "cooldown bright fill rises from bottom and reaches full");
    s.Apply({hud::Kind::Consumable,0,11,0},110);
    Check(s.Items()[0].count==11 && s.Items()[0].cooldownEnd==160 && s.CooldownRows(0,130)==13,
        "zero cooldown quantity update preserves running timer");
    s.Apply({hud::Kind::Consumable,1,2,0},110);
    Check(std::string_view(s.HealingCommand(5))=="useitem \"zbs3\"\n" &&
        std::string_view(s.HealingCommand(6))=="useitem \"zbs10\"\n","original healing commands, no fake revival command");
    for (int slot : {0,1,2,3,4,7,8,9,10,82}) Check(!s.HealingCommand(slot),"other keys and R remain native");
    s.SetMask(Bytes({0,0}));Check(std::string_view(s.HealingCommand(5))=="useitem \"zbs3\"\n",
        "late visibility update does not disable owned item input");
    s.SetMask(Bytes({15,0}));s.Apply({hud::Kind::Consumable,1,0,0},120);
    Check(std::string_view(s.HealingCommand(6)).empty(),"zero inventory does not request item");
    s.SetCenter({0,30},120,2);s.SetHint(0,120);s.Apply({hud::Kind::Outcome,0},120);
    Check(s.Center(120)&&s.Center(121.999)&&!s.Center(119)&&!s.Center(122),"countdown follows server duration and ignores backward time");
    Check(s.Hint(120)==0 && s.Hint(123)==-1,"hint expires independently from countdown");
    Check(s.OutcomeAlpha(119)==0 && s.OutcomeAlpha(120)==0 && s.OutcomeAlpha(120.25)==127 &&
        s.OutcomeAlpha(120.5)==255 && s.OutcomeAlpha(125.49)==255 && s.OutcomeAlpha(126)==127 &&
        s.OutcomeAlpha(126.5)==0,"original outcome .5/5/1 fade timing");
    s.ResetLife();
    Check(s.Active()&&s.Mask()==15&&s.Items()[0].count==11&&s.Items()[0].cooldownEnd==160,
        "death/respawn retains server consumables and cooldown");
    Check(!s.Center(120)&&s.Hint(120)<0&&s.OutcomeAlpha(120)==0,"new life retires previous prompts and completion");
    s.Reset();Check(!s.Active()&&!s.Mask()&&s.Items()[0].count==-1&&!s.HealingCommand(5),"map/session reset releases slot ownership and item data");
    Check(!s.Apply({hud::Kind::Outcome,2},1)&&!s.Active(),"invalid outcome cannot activate HUD");
    Check(!s.SetCenter({99,0},1,1)&&!s.SetCenter({0,0},-1,1)&&!s.SetCenter({0,0},1,-1),"bad center metadata rejected");
    const auto rect=hud::OutcomeRect(1024,768);
    Check(rect.x==307&&rect.y==129&&rect.width==410&&rect.height==73,"original banner centering rounds before scaling");
    const auto hd=hud::OutcomeRect(1920,1080);
    Check(hd.x==575&&hd.y==181&&hd.width==768&&hd.height==102,"original independent screen scaling");
    Check(hud::HintColor(2,30,0,.02)==std::array<int,3>{},"unrevealed hint letter is dark");
    Check(hud::HintColor(2,30,0,.2)==std::array<int,3>{39,254,39},"original glyph tint uses integer 256 divisor");
    Check(hud::HintColor(2,30,0,3)==std::array<int,3>{},"expired hint fade reaches black");
}
void Retry()
{
    hud::State state;
    for (int value = -128; value <= 127; ++value)
    {
        hud::Message decoded{hud::Kind::Outcome, 1, 77, 2};
        const bool valid = value >= -1 && value <= 10;
        const auto result = hud::DecodeMP(Bytes({27,15,unsigned(std::uint8_t(value))}), decoded);
        Check((result == hud::ParseResult::Accepted) == valid, "signed retry countdown range");
        if (valid)
        {
            Check(decoded.kind == hud::Kind::RetryCountdown && decoded.count == value && state.Apply(decoded, 100) &&
                state.RetryCountdown() == value, "server countdown retained without local clock prediction");
        }
        else Check(decoded.kind == hud::Kind::Outcome && decoded.count == 77, "invalid retry packet is transactional");
    }
    for (auto bytes : {Bytes({27,15}),Bytes({27,15,1,0}),Bytes({27,16,1}),Bytes({27,16,1,1,0}),
        Bytes({27,16,0,1}),Bytes({27,16,33,1}),Bytes({27,16,1,0})})
    { hud::Message decoded; Check(hud::DecodeMP(bytes,decoded)==hud::ParseResult::Malformed,"retry length and player/round bounds"); }
    for (unsigned player : {1u,32u}) for (unsigned round : {1u,255u})
    {
        hud::Message decoded;
        Check(hud::DecodeMP(Bytes({27,16,player,round}),decoded)==hud::ParseResult::Accepted &&
            decoded.kind==hud::Kind::RetryAnnouncement && decoded.index==player && decoded.count==int(round),
            "announcement keeps player and unsigned one-based round");
    }
    state.Apply({hud::Kind::RetryCountdown,0,10},100);
    state.ResetLife(); Check(state.RetryCountdown()==-1,"new life closes stale retry panel");
    state.Apply({hud::Kind::RetryCountdown,0,0},100);
    state.Apply({hud::Kind::Round},101); Check(state.RetryCountdown()==-1,"new round closes server zero panel");
    state.Apply({hud::Kind::RetryCountdown,0,2},100);
    state.Reset(); Check(state.RetryCountdown()==-1&&!state.Active(),"session reset clears retry ownership");
    const auto layout=hud::RetryRects(1024,768);
    Check(layout.banner.x==262&&layout.banner.y==120&&layout.banner.width==500&&layout.banner.height==90,
        "original retry banner reference bounds");
    Check(layout.numbers.x==442&&layout.numbers.y==210&&layout.numbers.width==140&&layout.numbers.height==140,
        "original retry number square and two-digit compressed layout");
    const auto wide=hud::RetryRects(1920,768);
    Check(wide.banner.x==710&&wide.banner.width==500&&wide.numbers.x==890,
        "aspect change recenters without stretching retry graphics");
    Check(std::string_view(hud::RetrySound(-1))=="zombi/TD_Heal.wav" && !hud::RetrySound(0) &&
        std::string_view(hud::RetrySound(10))=="vox/Ten.wav"&&!hud::RetrySound(11),"cancel and countdown sound dispatch");
    Check(hud::FormatRetryAnnouncement(L"[%s]玩家使用了回合重置。3秒后%d关将重新开始。",L"玩家%d%s",2)==
        L"[玩家%d%s]玩家使用了回合重置。3秒后2关将重新开始。","player name is data, not another formatting template");
}
}
int main()
{ test::Protocol();test::Lifecycle();test::Retry();std::cout<<"scenario_respawn_ui checks="<<test::checks<<" failures="<<test::failures<<'\n';return test::failures?1:0; }
