// SPDX-License-Identifier: GPL-3.0-or-later
// Local weapon-test controls; excluded from normal server builds.
#include "precompiled.h"
#include "weapons.h"
#include <array>
#include <string>

namespace
{
cvar_t enabled = { "csol_bot_test", "0", 0, 0, nullptr };
constexpr int PageSize = 7;

struct Target
{
    EHANDLE entity;
    int userid{};
};

struct Menu
{
    int userid{}, page{}, keys{};
    float expires{};
    bool list{};
    Target selected;
    std::array<Target, PageSize> entries;
};
std::array<Menu, MAX_CLIENTS + 1> menus;

bool IsSyPB(CBasePlayer* player)
{
    if (!player || player->IsDormant() || !(player->pev->flags & FL_FAKECLIENT)) return false;
#ifdef _WIN32
    // SyPB 1.50's public C export takes a one-based player index.
    const auto module = GetModuleHandleA("sypb.dll");
    const auto isBot = module ? reinterpret_cast<int (*)(int)>(GetProcAddress(module, "Amxx_IsSypb")) : nullptr;
    return isBot && isBot(player->entindex()) == 1;
#else
    return false;
#endif
}

Target Identify(CBasePlayer* player)
{
    Target result;
    result.entity = player;
    result.userid = GETPLAYERUSERID(player->edict());
    return result;
}

CBasePlayer* Resolve(Target& target)
{
    auto* player = static_cast<CBasePlayer*>(static_cast<CBaseEntity*>(target.entity));
    return IsSyPB(player) && GETPLAYERUSERID(player->edict()) == target.userid ? player : nullptr;
}

void Notice(CBasePlayer* player, const char* text)
{
    ClientPrint(player->pev, HUD_PRINTTALK, text);
}

std::string Name(CBasePlayer* player)
{
    std::string result = STRING(player->pev->netname);
    for (auto& c : result) if (static_cast<unsigned char>(c) < 32 || c == '\\') c = ' ';
    return result;
}

void SendMenu(CBasePlayer* player, Menu& menu, std::string text, int keys)
{
    menu.keys = keys;
    menu.expires = gpGlobals->time + 30;
    player->m_iMenu = Menu_OFF;
    // ShowMenu is bounded by the legacy 192-byte user-message limit. Split at
    // UTF-8 boundaries and let the original client assemble the full text.
    for (size_t offset = 0; offset < text.size();)
    {
        size_t end = Q_min(offset + 160, text.size());
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        std::string part = text.substr(offset, end - offset);
        ShowMenu(player, keys, 30, end < text.size(), part.data());
        offset = end;
    }
}

void ShowMain(CBasePlayer* player, Menu& menu)
{
    menu.list = false;
    auto* target = Resolve(menu.selected);
    std::string text = "\\y武器测试 / SyPB\\w\n\n1. ";
    text += CVAR_GET_FLOAT("sypb_stopbots") != 0 ? "恢复 Bot 行动\n" : "暂停 Bot 行动\n";
    text += "2. 选择 Bot 并传到面前\n3. 加入一个敌方 Bot\n4. 领取三重战术飞刀\n";
    int keys = MENU_KEY_1 | MENU_KEY_2 | MENU_KEY_3 | MENU_KEY_4 | MENU_KEY_0;
    if (target)
    {
        text += "5. 复活所选 Bot 并重新布置\n\n所选：" + Name(target);
        text += UTIL_VarArgs("  生命 %.0f / 护甲 %.0f\n", target->pev->health, target->pev->armorvalue);
        keys |= MENU_KEY_5;
    }
    text += "\n0. 关闭";
    SendMenu(player, menu, std::move(text), keys);
}

void ShowBots(CBasePlayer* player, Menu& menu)
{
    std::array<CBasePlayer*, MAX_CLIENTS> bots{};
    int count = 0;
    for (int i = 1; i <= gpGlobals->maxClients; ++i)
    {
        auto* bot = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(i));
        if (IsSyPB(bot) && (bot->m_iTeam == CT || bot->m_iTeam == TERRORIST)) bots[count++] = bot;
    }
    menu.page = Q_min(menu.page, count ? (count - 1) / PageSize : 0);
    menu.list = true;
    menu.entries = {};
    int keys = MENU_KEY_0;
    std::string text = "\\y选择 Bot：传到面前\\w\n\n";
    for (int n = 0; n < PageSize && menu.page * PageSize + n < count; ++n)
    {
        auto* bot = bots[menu.page * PageSize + n];
        menu.entries[n] = Identify(bot);
        text += std::to_string(n + 1) + ". " + Name(bot);
        text += bot->m_iTeam == player->m_iTeam ? " [同队]" : " [敌方]";
        if (!bot->IsAlive()) text += " [已死亡]";
        text += '\n';
        keys |= 1 << n;
    }
    if (!count) text += "暂无已入队的 SyPB Bot\n";
    if (menu.page > 0) { text += "\n8. 上一页"; keys |= MENU_KEY_8; }
    if ((menu.page + 1) * PageSize < count) { text += "\n9. 下一页"; keys |= MENU_KEY_9; }
    text += "\n0. 返回";
    SendMenu(player, menu, std::move(text), keys);
}

bool FindPosition(CBasePlayer* player, CBasePlayer* bot, Vector& result)
{
    UTIL_MakeVectors(Vector(0, player->pev->v_angle.y, 0));
    const Vector direction = gpGlobals->v_forward;
    for (float range : {128.0f, 112.0f, 96.0f, 80.0f, 64.0f})
    {
        const Vector start = player->pev->origin + direction * 40 + Vector(0, 0, 18);
        const Vector end = player->pev->origin + direction * range + Vector(0, 0, 18);
        TraceResult path, floor, occupied, sight;
        UTIL_TraceHull(start, end, dont_ignore_monsters, human_hull, bot->edict(), &path);
        if (path.fStartSolid || path.fAllSolid || path.flFraction != 1) continue;
        UTIL_TraceHull(end, end - Vector(0, 0, 96), dont_ignore_monsters, human_hull, bot->edict(), &floor);
        if (floor.fStartSolid || floor.fAllSolid || floor.flFraction == 1 || floor.vecPlaneNormal.z < 0.7f) continue;
        const Vector destination = floor.vecEndPos + Vector(0, 0, 1);
        UTIL_TraceHull(destination, destination, dont_ignore_monsters, human_hull, bot->edict(), &occupied);
        if (occupied.fStartSolid || occupied.fAllSolid || occupied.flFraction != 1) continue;
        UTIL_TraceLine(player->EyePosition(), destination + Vector(0, 0, 28), ignore_monsters, player->edict(), &sight);
        if (sight.flFraction != 1) continue;
        result = destination;
        return true;
    }
    return false;
}

void PlaceBot(CBasePlayer* player, Target& target, bool respawn)
{
    auto* bot = Resolve(target);
    if (!bot) { Notice(player, "[武器测试] 所选 Bot 已离开，请重新选择。"); return; }
    if (!player->IsAlive()) { Notice(player, "[武器测试] 请先复活自己。"); return; }
    Vector position;
    if (!FindPosition(player, bot, position))
    {
        Notice(player, "[武器测试] 面前没有可安全站立的位置，请移到空旷处。");
        return;
    }
    CVAR_SET_FLOAT("sypb_stopbots", 1);
    if (respawn || !bot->IsAlive())
    {
        bot->RoundRespawn();
        // CBasePlayer::Spawn clears FL_FAKECLIENT; SyPB normally restores it
        // on its next Think. Preserve this verified bot's identity immediately
        // so the refreshed menu can still resolve the selected target.
        bot->pev->flags |= FL_FAKECLIENT;
    }
    if (!bot->IsAlive()) { Notice(player, "[武器测试] Bot 尚未完成入队，稍后重试。"); return; }
    bot->RemoveSpawnProtection();
    bot->pev->flags &= ~FL_DUCKING;
    bot->pev->bInDuck = false;
    bot->m_flDuckTime = 0;
    UTIL_SetSize(bot->pev, VEC_HULL_MIN, VEC_HULL_MAX);
    bot->pev->view_ofs = VEC_VIEW;
    bot->pev->velocity = bot->pev->basevelocity = g_vecZero;
    bot->pev->button = bot->pev->oldbuttons = 0;
    bot->pev->v_angle = UTIL_VecToAngles(player->pev->origin - position);
    bot->pev->v_angle.x = bot->pev->v_angle.z = 0;
    bot->pev->angles = bot->pev->v_angle;
    bot->pev->fixangle = 1;
    UTIL_SetOrigin(bot->pev, position);
    Notice(player, "[武器测试] Bot 已暂停并传到面前，可以攻击测试。");
    SERVER_PRINT(UTIL_VarArgs("[CSOLBotTest] placed player=%d target=%d userid=%d x=%.3f y=%.3f z=%.3f\n",
        player->entindex(), bot->entindex(), target.userid, position.x, position.y, position.z));
}

void Fixture()
{
    const char* action = CMD_ARGV(1);
    if (!Q_strcmp(action, "status"))
    {
        SERVER_PRINT(UTIL_VarArgs("[CSOLBotTest] enabled=%d paused=%d weaponsReady=%d\n", enabled.value != 0,
            CVAR_GET_FLOAT("sypb_stopbots") != 0, CSOL_WeaponsReady()));
        for (int i = 1; i <= gpGlobals->maxClients; ++i)
        {
            auto* player = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(i));
            if (!player || player->IsDormant()) continue;
            SERVER_PRINT(UTIL_VarArgs("[CSOLBotTest] player=%d userid=%d sypb=%d team=%d alive=%d health=%.3f armor=%.3f frags=%.0f deaths=%d x=%.3f y=%.3f z=%.3f menu=%d list=%d selected=%d\n",
                i, GETPLAYERUSERID(player->edict()), IsSyPB(player), player->m_iTeam, player->IsAlive(), player->pev->health,
                player->pev->armorvalue, player->pev->frags, player->m_iDeaths,
                player->pev->origin.x, player->pev->origin.y, player->pev->origin.z,
                menus[i].expires > gpGlobals->time, menus[i].list, menus[i].selected.userid));
        }
    }
    else if ((!Q_strcmp(action, "menu") && CMD_ARGC() == 3) || (!Q_strcmp(action, "select") && CMD_ARGC() == 4))
    {
        const int index = Q_atoi(CMD_ARGV(2));
        if (index < 1 || index > gpGlobals->maxClients) return;
        auto* player = static_cast<CBasePlayer*>(UTIL_PlayerByIndex(index));
        if (!player || player->IsDormant()) return;
        CSOL_BotTestCommand(player->edict(), !Q_strcmp(action, "menu") ? "csol_bot_menu" : "menuselect", CMD_ARGV(3));
    }
}
}

void CSOL_InitBotTests() { CVAR_REGISTER(&enabled); ADD_SERVER_COMMAND("csol_bot_fixture", Fixture); }
void CSOL_ResetBotTests() { menus = {}; }

bool CSOL_BotTestCommand(edict_t* entity, const char* command, const char* argument)
{
    auto* player = CBasePlayer::Instance(entity);
    if (!player || player->IsDormant() || (player->pev->flags & FL_FAKECLIENT)) return false;
    const int index = player->entindex();
    if (index < 1 || index > MAX_CLIENTS) return false;
    auto& menu = menus[index];
    const int userid = GETPLAYERUSERID(entity);
    const bool open = !Q_strcmp(command, "csol_bot_menu") ||
        (!Q_strcmp(command, "say") && (!Q_strcmp(argument, "/bottest") || !Q_strcmp(argument, "\"/bottest\"")));
    if (open)
    {
        if (enabled.value == 0 || !CVAR_GET_POINTER("sypb_stopbots"))
        { Notice(player, "[武器测试] 当前服务器未启用 SyPB 测试菜单。"); return true; }
        if (player->m_iJoiningState != JOINED) return true;
        if (menu.userid != userid) menu = {};
        menu.userid = userid;
        ShowMain(player, menu);
        return true;
    }
    if (!Q_strcmp(command, "menuselect"))
    {
        if (enabled.value == 0 || menu.userid != userid || menu.expires <= gpGlobals->time || player->m_iMenu != Menu_OFF)
        { menu = {}; return false; }
        const int choice = Q_atoi(argument);
        if (choice < 1 || choice > 10 || !(menu.keys & (1 << (choice - 1)))) return true;
        if (menu.list)
        {
            if (choice == 8 || choice == 9)
            { menu.page += choice == 8 ? -1 : 1; ShowBots(player, menu); return true; }
            if (choice <= PageSize)
            { menu.selected = menu.entries[choice - 1]; PlaceBot(player, menu.selected, false); }
        }
        else
        {
            switch (choice)
            {
            case 1: CVAR_SET_FLOAT("sypb_stopbots", CVAR_GET_FLOAT("sypb_stopbots") == 0 ? 1 : 0); break;
            case 2: menu.page = 0; ShowBots(player, menu); return true;
            case 3:
                if (player->m_iTeam == CT || player->m_iTeam == TERRORIST)
                    SERVER_COMMAND(player->m_iTeam == CT ? "sypb_add_t\n" : "sypb_add_ct\n");
                break;
            case 4:
                if (!CSOL_WeaponsReady())
                    Notice(player, "[武器测试] 本图未预缓存飞刀，请管理员开启 csol_weapons 后换图。");
                else if (player->IsAlive())
                {
                    if (!player->GetItemById(static_cast<WeaponIdType>(CSOL_WEAPON_TKNIFEEX2)))
                    { player->DropSecondary(); player->GiveNamedItemEx("weapon_tknifeex2"); }
                    player->SelectItem("weapon_tknifeex2");
                }
                break;
            case 5: PlaceBot(player, menu.selected, true); break;
            case 10:
                // Keep the identified target while the player closes the menu
                // to shoot. Reopening can then revive that same bot.
                menu.expires = 0;
                menu.keys = 0;
                menu.entries = {};
                ShowMenu(player, 0, 0, FALSE, "");
                return true;
            }
        }
        ShowMain(player, menu);
        return true;
    }
    // A native menu takes ownership as soon as its command is issued.
    if (!Q_strcmp(command, "chooseteam") || !Q_strcmp(command, "buy") || !Q_strncmp(command, "radio", 5)) menu = {};
    return false;
}
