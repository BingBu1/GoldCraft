// SPDX-License-Identifier: GPL-3.0-or-later
// Preserve and execute the existing complete round-HUD regression, then test
// local original notices sharing its actual channel/lifetime implementation.
#define wmain ExistingRoundChecks
#include "../csol-scenario-round-hud/client_main.cpp"
#undef wmain

int wmain(int argc, wchar_t** argv)
{
    if (ExistingRoundChecks(argc, argv)) return 1;
    using namespace fixture; using namespace csol::client;
    const auto before = checks;
    ResetScenarioRoundHud();
    Check(ShowScenarioRoundHudText(6,L"道具冷却时间剩余45秒，无法立即复活",{255,195,35}),"original local channel6 accepts localized text");
    const auto notice = state.Notices()[6];
    Check(notice.active && notice.color==std::array<std::uint8_t,3>{255,195,35} && notice.x==.5f && notice.y==.391f,
        "local original notice preserves channel6 color and placement");
    Check(std::abs(notice.fadeEnd-notice.born-.1f)<.0001f && std::abs(notice.showEnd-notice.fadeEnd-1.1f)<.0001f &&
        std::abs(notice.die-notice.showEnd-.1f)<.0001f,"local original notice shares .1/1.1/.1 channel timing");
    Check(localLines[6]==std::vector<std::wstring>{L"道具冷却时间剩余45秒，无法立即复活"},"local text is held separately from map row IDs");
    DrawScenarioRoundHud(clientTime,0);Check(requested&&!catalogLoaded&&!state.Active(),"local-only notice draws without clock or map catalog");
    Check(!ShowScenarioRoundHudText(20,L"invalid",{1,2,3})&&!ShowScenarioRoundHudText(6,L"",{1,2,3})&&
        !ShowScenarioRoundHudText(6,std::wstring(1025,L'x'),{1,2,3})&&state.Notices()[6].color==notice.color,
        "invalid local notice preserves previous valid channel");
    Check(ShowScenarioRoundHudText(6,L"第一行\r\n第二行",{1,2,3})&&localLines[6]==std::vector<std::wstring>{L"第一行",L"第二行"},
        "local multiline split matches existing map text convention");
    LoadCatalog();
    const auto* row = messages.Find(193);
    Check(row&&(row->textType==-1||row->textType==1)&&!row->text.empty(),"actual deadend opening message uses channel1");
    if(row)
    {
        const auto id=row->id;
        ShowScenarioRoundHudText(1,L"本地提示",{255,195,35});
        Receive("MPToCL",4,Bytes({27,7,id&255u,unsigned(id)>>8}).data());
        Check(localLines[1].empty()&&state.Notices()[1].id==id&&!localLines[6].empty(),
            "later map dialogue replaces matching local channel without clearing independent channel6");
    }
    ShowScenarioRoundHudText(6,L"冷却",{255,195,35});ResetLife("ResetHUD",0,nullptr);
    Check(localLines[6].empty()&&!state.Notices()[6].active,"life reset clears local notices together with map text");
    ShowScenarioRoundHudText(6,L"冷却",{255,195,35});ResetScenarioRoundHud();
    Check(localLines[6].empty()&&!state.Notices()[6].active,"session reset clears local override");
    std::cout<<"scenario_respawn_round_text newChecks="<<checks-before<<" totalChecks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
