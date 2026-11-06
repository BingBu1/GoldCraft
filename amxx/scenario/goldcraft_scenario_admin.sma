// SPDX-License-Identifier: GPL-3.0-or-later
#include <amxmodx>
#include <amxmisc>
#include <reapi>
#include <csol_game_mode>
#include <csol_scenario_admin>

#define PLUGIN_NAME "GoldCraft 大灾变管理"

new g_FinishAction;
new g_NextMap;
new g_Menu[33];
new g_MenuMap[33];
// Original cso_chn localization, CSO_ZBS_DIFF_GRADE1..7.
new const g_DifficultyNames[][] = { "", "入门", "普通", "困难", "噩梦", "地狱", "炼狱", "炼狱Ⅰ" };

public plugin_init()
{
    for (new id; id < sizeof g_Menu; id++) g_Menu[id] = -1;
    register_plugin(PLUGIN_NAME, "0.5.0", "GoldCraft contributors");
    register_clcmd("chooseteam", "CommandChooseTeam", -1, "", 0);
    register_concmd("amx_scenario_menu", "CommandMenu", ADMIN_CFG, "打开大灾变管理", 0);
    register_concmd("amx_scenario_difficulty", "CommandDifficulty", ADMIN_CFG, "[1..7] 设置下次重玩难度", 0);
    register_concmd("amx_scenario_replay", "CommandReplay", ADMIN_CFG, "按待用难度重玩当前地图，无需重新载图", 0);
    register_concmd("amx_scenario_vote", "CommandMapVote", ADMIN_CFG, "立即发起可视化选图投票", 0);
    register_concmd("amx_scenario_finish", "CommandFinish", ADMIN_CFG, "<0=留图游玩|1=换图|2=同图重玩|3=投票换图>", 0);
    register_concmd("amx_scenario_nextmap", "CommandNextMap", ADMIN_CFG, "<地图名> 设置通关后的目标地图", 0);
    g_FinishAction = get_cvar_pointer("csol_scenario_finish_action");
    g_NextMap = get_cvar_pointer("csol_scenario_nextmap");
    if (!g_FinishAction || !g_NextMap || csol_scenario_admin_api_version() != 1 ||
        csol_scenario_replay_api_version() != 1 || csol_scenario_map_vote_api_version() != 1)
        set_fail_state("服务端缺少大灾变管理接口，请更新配套 mp.dll 和 GoldCraft AMXX 模块");
}

CloseFinishMenu(id)
{
    new menu = g_Menu[id];
    g_Menu[id] = -1;
    // Destroy may synchronously cancel a displayed menu. Drop ownership first
    // so that its handler cannot destroy this same menu a second time.
    if (menu != -1) menu_destroy(menu);
}

public client_disconnected(id)
{
    CloseFinishMenu(id);
}

public plugin_end()
{
    for (new id = 1; id < sizeof g_Menu; id++) CloseFinishMenu(id);
}

public plugin_cfg()
{
    if (ScenarioActive() && is_plugin_loaded("Menus Front-End") != -1)
        AddMenuItem("大灾变管理", "amx_scenario_menu", ADMIN_CFG, PLUGIN_NAME);
}

bool:ScenarioActive()
{
    return csol_game_mode_api_version() == 1 && csol_game_mode_current() == _:CSOL_MODE_SCENARIO;
}

public CommandChooseTeam(id)
{
    // M retains its native chooseteam binding. Only a joined scenario admin
    // consumes the command; initial character selection must still finish.
    if (id < 1 || id > 32 || !is_user_connected(id) || !ScenarioActive() ||
        !(get_user_flags(id) & ADMIN_CFG) || get_member(id, m_iJoiningState) != JOINED)
        return PLUGIN_CONTINUE;
    ShowFinishMenu(id);
    return PLUGIN_HANDLED;
}

bool:MapNameValid(const name[])
{
    new length = strlen(name);
    if (length < 1 || length > 63) return false;
    for (new i; i < length; i++)
    {
        new c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

public CommandMenu(id, level, cid)
{
    if (!cmd_access(id, level, cid, 1)) return PLUGIN_HANDLED;
    if (!ScenarioActive())
    {
        console_print(id, "当前不是大灾变模式。");
        return PLUGIN_HANDLED;
    }
    if (!id)
    {
        console_print(id, "控制台可用 amx_scenario_vote 立即投票，或 amx_scenario_difficulty、amx_scenario_finish、amx_scenario_nextmap。");
        return PLUGIN_HANDLED;
    }
    ShowFinishMenu(id);
    return PLUGIN_HANDLED;
}

ShowFinishMenu(id)
{
    CloseFinishMenu(id);
    new action[16], map[128], title[256];
    get_pcvar_string(g_FinishAction, action, charsmax(action));
    get_pcvar_string(g_NextMap, map, charsmax(map));
    if (!MapNameValid(map)) copy(map, charsmax(map), "地图名无效");
    new stateText[48];
    if (equal(action, "0")) copy(stateText, charsmax(stateText), "留在本图游玩");
    else if (equal(action, "1")) copy(stateText, charsmax(stateText), "切换目标地图");
    else if (equal(action, "2")) copy(stateText, charsmax(stateText), "同图重新挑战");
    else if (equal(action, "3")) copy(stateText, charsmax(stateText), "投票选择下一张地图");
    else copy(stateText, charsmax(stateText), "设置无效，将留图游玩");
    formatex(title, charsmax(title), "大灾变管理^n通关：%s^n目标：%s^n通关策略在结算等待结束时生效", stateText, map);
    new menu = menu_create(title, "FinishMenuHandler");
    g_Menu[id] = menu;
    new active, requested, maximum, mapId;
    g_MenuMap[id] = DifficultyInfo(active, requested, maximum, mapId) ? mapId : 0;
    menu_additem(menu, "通关后留在本图游玩");
    menu_additem(menu, "通关后切换目标地图");
    menu_additem(menu, "通关后重新挑战本图");
    menu_additem(menu, "查看目标地图设置命令");
    menu_additem(menu, "大灾变难度（下次重玩生效）");
    menu_additem(menu, "立即重玩本图（应用待用难度）");
    menu_additem(menu, "通关后投票切换地图（默认）");
    menu_additem(menu, "立即发起选图投票");
    // Keep all eight actions visible; the default seven-item page would hide
    // the direct vote on a second page. The matching AMXX API permits this.
    menu_setprop(menu, MPROP_PERPAGE, 0);
    menu_setprop(menu, MPROP_EXIT, MEXIT_FORCE);
    menu_setprop(menu, MPROP_EXITNAME, "关闭");
    menu_display(id, menu);
}

public FinishMenuHandler(id, menu, item)
{
    if (id < 1 || id >= sizeof g_Menu || g_Menu[id] != menu) return PLUGIN_HANDLED;
    new mapId = g_MenuMap[id];
    g_Menu[id] = -1;
    menu_destroy(menu);
    // Menu access can change while the menu is open. Never trust the earlier
    // command check or let a disconnected client/other mode submit a change.
    if (item < 0 || !is_user_connected(id) || !(get_user_flags(id) & ADMIN_CFG) || !ScenarioActive())
        return PLUGIN_HANDLED;
    switch (item)
    {
        case 0: set_pcvar_string(g_FinishAction, "0");
        case 1: set_pcvar_string(g_FinishAction, "1");
        case 2: set_pcvar_string(g_FinishAction, "2");
        case 6: set_pcvar_string(g_FinishAction, "3");
        case 3:
        {
            client_print(id, print_chat, "[大灾变] 控制台：amx_scenario_nextmap <地图名>，目标须为已接入的灾变地图。");
            ShowFinishMenu(id);
            return PLUGIN_HANDLED;
        }
        case 4:
        {
            ShowDifficultyMenu(id);
            return PLUGIN_HANDLED;
        }
        case 5:
        {
            SubmitReplay(id, mapId);
            return PLUGIN_HANDLED;
        }
        case 7:
        {
            SubmitMapVote(id, mapId);
            return PLUGIN_HANDLED;
        }
        default: return PLUGIN_HANDLED;
    }
    log_amx("管理员 %d 修改大灾变通关策略，菜单选项 %d", get_user_userid(id), item);
    client_print(id, print_chat, "[大灾变] 设置已保存到当前服务器；将在通关结算等待结束时读取。");
    ShowFinishMenu(id);
    return PLUGIN_HANDLED;
}

bool:DifficultyInfo(&active, &requested, &maximum, &mapId)
{
    return ScenarioActive() && csol_scenario_difficulty_info(active, requested, maximum, mapId)
        && maximum >= 1 && maximum < sizeof g_DifficultyNames
        && active >= 1 && active <= maximum && requested >= 0 && requested <= maximum;
}

ShowDifficultyMenu(id)
{
    new active, requested, maximum, mapId;
    if (!DifficultyInfo(active, requested, maximum, mapId))
    {
        client_print(id, print_chat, "[大灾变] 当前地图难度资源尚未就绪，暂时无法设置。");
        return;
    }
    CloseFinishMenu(id);
    new title[256], pending[48];
    if (requested) copy(pending, charsmax(pending), g_DifficultyNames[requested]);
    else copy(pending, charsmax(pending), "待用设置无效");
    formatex(title, charsmax(title), "大灾变难度^n当前：%s^n待用：%s^n选择后在管理页重玩本图即可生效",
        g_DifficultyNames[active], pending);
    new menu = menu_create(title, "DifficultyMenuHandler");
    g_Menu[id] = menu;
    g_MenuMap[id] = mapId;
    new label[96], value[4];
    for (new difficulty = 1; difficulty <= maximum; difficulty++)
    {
        formatex(label, charsmax(label), "%s%s%s", g_DifficultyNames[difficulty],
            difficulty == active ? " [当前]" : "", difficulty == requested ? " [待用]" : "");
        num_to_str(difficulty, value, charsmax(value));
        menu_additem(menu, label, value);
    }
    menu_setprop(menu, MPROP_EXITNAME, "关闭");
    menu_display(id, menu);
}

public DifficultyMenuHandler(id, menu, item)
{
    if (id < 1 || id >= sizeof g_Menu || g_Menu[id] != menu) return PLUGIN_HANDLED;
    new value[4], access, callback;
    new bool:hasItem = item >= 0 && menu_item_getinfo(menu, item, access, value, charsmax(value), _, _, callback);
    new mapId = g_MenuMap[id];
    g_Menu[id] = -1;
    menu_destroy(menu);
    if (!hasItem || !is_user_connected(id) || !(get_user_flags(id) & ADMIN_CFG) || !ScenarioActive())
        return PLUGIN_HANDLED;
    new difficulty = str_to_num(value);
    if (!csol_scenario_difficulty_request(mapId, difficulty))
        client_print(id, print_chat, "[大灾变] 难度设置未生效：地图状态已改变或该档位不可用，请重新打开菜单。");
    else
    {
        log_amx("管理员 %d 设置地图 %d 的待用难度为 %d", get_user_userid(id), mapId, difficulty);
        client_print(id, print_chat, "[大灾变] 待用难度已设置；选择“立即重玩本图”后生效，无需重新载图。");
    }
    ShowDifficultyMenu(id);
    return PLUGIN_HANDLED;
}

public CommandDifficulty(id, level, cid)
{
    if (!cmd_access(id, level, cid, 1)) return PLUGIN_HANDLED;
    new active, requested, maximum, mapId;
    if (!DifficultyInfo(active, requested, maximum, mapId))
    {
        console_print(id, "当前不是大灾变，或地图难度资源尚未就绪。");
        return PLUGIN_HANDLED;
    }
    if (read_argc() == 1)
    {
        if (id) ShowDifficultyMenu(id);
        else console_print(id, "当前难度 %d，待用难度 %d（0表示无效），本图允许 1..%d；用 amx_scenario_difficulty <编号> 设置。", active, requested, maximum);
        return PLUGIN_HANDLED;
    }
    new value[32];
    read_argv(1, value, charsmax(value));
    if (read_argc() != 2 || value[0] < '1' || value[0] > '7' || value[1] ||
        !csol_scenario_difficulty_request(mapId, value[0] - '0'))
    {
        console_print(id, "设置未生效；本图只允许 1..%d 的单个整数难度编号。", maximum);
        return PLUGIN_HANDLED;
    }
    log_amx("管理员 %d 设置地图 %d 的待用难度为 %s", id ? get_user_userid(id) : 0, mapId, value);
    console_print(id, "待用难度已设为 %s；用 amx_scenario_replay 重玩本图后生效，无需重新载图。", g_DifficultyNames[value[0] - '0']);
    return PLUGIN_HANDLED;
}

SubmitReplay(id, mapId)
{
    if (!mapId || !csol_scenario_replay(mapId))
    {
        console_print(id, "无法重玩：地图状态已改变、难度资源无效或已有重玩请求。");
        if (id) client_print(id, print_chat, "[大灾变] 重玩请求未接受，请重新打开管理菜单。");
        return;
    }
    log_amx("管理员 %d 请求按待用难度重玩地图 %d", id ? get_user_userid(id) : 0, mapId);
    console_print(id, "已请求重玩当前地图，服务端将在安全帧重置关卡、资金与升级。");
    client_print(0, print_chat, "[大灾变] 管理员已请求按待用难度重新挑战本图。");
}

public CommandReplay(id, level, cid)
{
    if (!cmd_access(id, level, cid, 1)) return PLUGIN_HANDLED;
    new active, requested, maximum, mapId;
    if (!DifficultyInfo(active, requested, maximum, mapId))
    {
        console_print(id, "当前不是已就绪的大灾变地图。");
        return PLUGIN_HANDLED;
    }
    SubmitReplay(id, mapId);
    return PLUGIN_HANDLED;
}

SubmitMapVote(id, mapId)
{
    if (!mapId || !csol_scenario_map_vote(mapId))
    {
        console_print(id, "无法发起投票：地图尚未就绪、状态已改变，或已有投票／重玩请求。");
        if (id) client_print(id, print_chat, "[大灾变] 选图投票请求未接受，请重新打开管理菜单。");
        return;
    }
    log_amx("管理员 %d 请求立即发起地图 %d 的选图投票", id ? get_user_userid(id) : 0, mapId);
    console_print(id, "选图投票请求已接受，服务端将在安全帧检查候选地图并发起投票。");
    client_print(0, print_chat, "[大灾变] 管理员已请求发起选图投票。");
}

public CommandMapVote(id, level, cid)
{
    if (!cmd_access(id, level, cid, 1)) return PLUGIN_HANDLED;
    if (read_argc() != 1)
    {
        console_print(id, "用法：amx_scenario_vote（无需参数）。");
        return PLUGIN_HANDLED;
    }
    if (id && (id < 1 || id > 32 || !is_user_connected(id))) return PLUGIN_HANDLED;
    new active, requested, maximum, mapId;
    if (!DifficultyInfo(active, requested, maximum, mapId))
    {
        console_print(id, "当前不是已就绪的大灾变地图。");
        return PLUGIN_HANDLED;
    }
    // Destroy can synchronously call a menu handler. Capture mapId before it,
    // then recheck access; the service rejects a stale map or duplicate vote.
    if (id) CloseFinishMenu(id);
    if (!ScenarioActive() || (id && (!is_user_connected(id) || !(get_user_flags(id) & ADMIN_CFG))))
        return PLUGIN_HANDLED;
    SubmitMapVote(id, mapId);
    return PLUGIN_HANDLED;
}

public CommandFinish(id, level, cid)
{
    if (!cmd_access(id, level, cid, 2)) return PLUGIN_HANDLED;
    if (!ScenarioActive()) { console_print(id, "当前不是大灾变模式。"); return PLUGIN_HANDLED; }
    new action[64];
    read_argv(1, action, charsmax(action));
    if (!equal(action, "0") && !equal(action, "1") && !equal(action, "2") && !equal(action, "3"))
    {
        console_print(id, "仅允许 0（留图游玩）、1（切换目标地图）、2（同图重玩）或 3（投票换图）。");
        return PLUGIN_HANDLED;
    }
    set_pcvar_string(g_FinishAction, action);
    log_amx("管理员 %d 设置 csol_scenario_finish_action=%s", id ? get_user_userid(id) : 0, action);
    console_print(id, "通关策略已设置为 %s，将在结算等待结束时读取。", action);
    return PLUGIN_HANDLED;
}

public CommandNextMap(id, level, cid)
{
    if (!cmd_access(id, level, cid, 2)) return PLUGIN_HANDLED;
    if (!ScenarioActive()) { console_print(id, "当前不是大灾变模式。"); return PLUGIN_HANDLED; }
    new map[128];
    read_argv(1, map, charsmax(map));
    if (!MapNameValid(map) || !is_map_valid(map))
    {
        console_print(id, "地图不存在或名称无效；请填写不含路径及扩展名的地图名（最多 63 字符）。");
        return PLUGIN_HANDLED;
    }
    // C++ checks the scenario catalog and implemented gameplay at completion.
    // This command does not run changelevel or parse any gameplay resources.
    set_pcvar_string(g_NextMap, map);
    log_amx("管理员 %d 设置 csol_scenario_nextmap=%s", id ? get_user_userid(id) : 0, map);
    console_print(id, "目标已设置为 %s；通关时服务端将验证灾变支持。此命令不立即换图。", map);
    return PLUGIN_HANDLED;
}
