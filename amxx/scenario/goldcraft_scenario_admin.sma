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
new g_MenuUserid[33];
new g_MenuRevision[33];
new g_InputUserid[33], g_InputRevision[33], g_InputSetting[33], g_InputType[33];
new g_InputKey[33][64];
new g_PendingUserid[33], g_PendingRevision[33], g_PendingSetting[33], g_PendingPolls[33];
new Float:g_PendingValue[33];
const TASK_CUSTOM_SAVE = 27400;
new const g_CustomKeys[][] = {
    "kick",
    "time_cycle",
    "blind_box",
    "mastery",
    "cof_sprint",
    "blood",
    "dusk_buff",
    "night_buff",
    "zombie_population",
    "random_horde",
    "damage_refund_cap",
    "midday_seconds",
    "dusk_seconds",
    "blind_box_price_1",
    "blind_box_price_2",
    "blind_box_price_3",
    "mastery_threshold_1",
    "mastery_threshold_2",
    "mastery_threshold_3",
    "mastery_reload_1",
    "mastery_reload_2",
    "mastery_damage_2",
    "mastery_damage_3",
    "mastery_fire_rate_3",
    "sprint_maximum",
    "sprint_drain_per_second",
    "sprint_recovery_per_second",
    "sprint_speed",
    "walk_speed",
    "jump_cost",
    "kick_cost",
    "dusk_maximum_multiplier",
    "dusk_recovery_multiplier",
    "blood_lifetime_seconds",
    "blood_maximum",
    "dusk_speed_multiplier",
    "dusk_regen_per_second",
    "night_damage_multiplier",
    "night_speed_multiplier",
    "night_regen_per_second",
    "midday_zombie_base",
    "dusk_zombie_base",
    "night_zombie_base",
    "zombies_per_player",
    "horde_interval_seconds",
    "horde_probability",
    "horde_duration_seconds",
    "damage_refund_max_per_monster"
};
new const g_CustomNames[][] = {
    "范围踢击",
    "时间系统",
    "武器盲盒",
    "武器熟练度",
    "体力与奔跑",
    "血迹效果",
    "黄昏强化",
    "永夜强化",
    "僵尸数量",
    "随机尸潮",
    "伤害返金限制",
    "正午时长",
    "黄昏时长",
    "CoF盲盒价格",
    "CS盲盒价格",
    "CSOL盲盒价格",
    "熟练度一档击杀",
    "熟练度二档击杀",
    "熟练度三档击杀",
    "一档换弹倍率",
    "二档换弹倍率",
    "二档伤害倍率",
    "三档伤害倍率",
    "三档射速倍率",
    "体力上限",
    "奔跑体力消耗",
    "体力恢复速度",
    "奔跑速度",
    "普通行走速度",
    "跳跃体力消耗",
    "踢击体力消耗",
    "黄昏体力倍率",
    "黄昏恢复倍率",
    "血迹保留时长",
    "血迹数量上限",
    "黄昏移速倍率",
    "黄昏每秒回血",
    "永夜伤害倍率",
    "永夜移速倍率",
    "永夜每秒回血",
    "正午基础怪量",
    "黄昏基础怪量",
    "永夜基础怪量",
    "每人增加怪量",
    "尸潮抽样间隔",
    "尸潮触发概率",
    "尸潮持续时长",
    "单怪伤害返金上限"
};

// Original cso_chn localization, CSO_ZBS_DIFF_GRADE1..7.
new const g_DifficultyNames[][] = { "", "入门", "普通", "困难", "噩梦", "地狱", "炼狱", "炼狱Ⅰ" };

public plugin_init()
{
    for (new id; id < sizeof g_Menu; id++) g_Menu[id] = -1;
    register_plugin(PLUGIN_NAME, "0.6.1", "GoldCraft contributors");
    register_clcmd("chooseteam", "CommandChooseTeam", -1, "", 0);
    register_concmd("amx_scenario_menu", "CommandMenu", ADMIN_CFG, "打开大灾变管理", 0);
    register_concmd("amx_scenario_difficulty", "CommandDifficulty", ADMIN_CFG, "[1..7] 设置下次重玩难度", 0);
    register_concmd("amx_scenario_replay", "CommandReplay", ADMIN_CFG, "按待用难度重玩当前地图，无需重新载图", 0);
    register_concmd("amx_scenario_vote", "CommandMapVote", ADMIN_CFG, "立即发起可视化选图投票", 0);
    register_concmd("amx_scenario_finish", "CommandFinish", ADMIN_CFG, "<0=留图游玩|1=换图|2=同图重玩|3=投票换图>", 0);
    register_concmd("amx_scenario_nextmap", "CommandNextMap", ADMIN_CFG, "<地图名> 设置通关后的目标地图", 0);
    register_clcmd("csol_custom_value", "CommandCustomValue", -1, "", 0);
    g_FinishAction = get_cvar_pointer("csol_scenario_finish_action");
    g_NextMap = get_cvar_pointer("csol_scenario_nextmap");
    if (!g_FinishAction || !g_NextMap || csol_scenario_admin_api_version() != 1 ||
        csol_scenario_replay_api_version() != 1 || csol_scenario_map_vote_api_version() != 1 ||
        csol_custom_api_version() != 1)
        set_fail_state("服务端缺少大灾变管理接口，请更新配套 mp.dll 和 GoldCraft AMXX 模块");
}

CloseFinishMenu(id)
{
    ClearCustomInput(id);
    new menu = g_Menu[id];
    g_Menu[id] = -1;
    // Destroy may synchronously cancel a displayed menu. Drop ownership first
    // so that its handler cannot destroy this same menu a second time.
    if (menu != -1) menu_destroy(menu);
}

public client_disconnected(id)
{
    ClearCustomInput(id);
    ClearCustomPending(id);
    CloseFinishMenu(id);
}

public plugin_end()
{
    for (new id = 1; id < sizeof g_Menu; id++)
    {
        ClearCustomInput(id);
        ClearCustomPending(id);
        CloseFinishMenu(id);
    }
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
    // Retain the native M binding and initial selection in every mode.
    if (!ScenarioJoined(id))
        return PLUGIN_CONTINUE;
    ClearCustomInput(id);
    ShowPlayerMenu(id);
    return PLUGIN_HANDLED;
}

bool:ScenarioJoined(id)
{
    return id >= 1 && id <= 32 && is_user_connected(id) && ScenarioActive() &&
        get_member(id, m_iJoiningState) == JOINED;
}

bool:Administrator(id, userid = 0)
{
    return ScenarioJoined(id) && (get_user_flags(id) & ADMIN_CFG) &&
        (!userid || get_user_userid(id) == userid);
}

ShowPlayerMenu(id)
{
    if (!ScenarioJoined(id)) return;
    CloseFinishMenu(id);
    new menu = menu_create("大灾变^n玩家功能", "PlayerMenuHandler");
    g_Menu[id] = menu;
    g_MenuUserid[id] = get_user_userid(id);
    menu_additem(menu, "购买系统", "0");
    if (Administrator(id)) menu_additem(menu, "管理员菜单", "2");
    menu_setprop(menu, MPROP_EXITNAME, "关闭");
    menu_display(id, menu);
}

public PlayerMenuHandler(id, menu, item)
{
    if (id < 1 || id > 32 || g_Menu[id] != menu) return PLUGIN_HANDLED;
    new userid = g_MenuUserid[id], value[4], access, callback;
    new bool:hasItem = item >= 0 && menu_item_getinfo(menu, item, access, value, charsmax(value), _, _, callback);
    g_Menu[id] = -1;
    menu_destroy(menu);
    if (!hasItem || !ScenarioJoined(id) || get_user_userid(id) != userid) return PLUGIN_HANDLED;
    if (equal(value, "2"))
    {
        if (Administrator(id, userid)) ShowFinishMenu(id);
        return PLUGIN_HANDLED;
    }
    if (equal(value, "0")) engclient_cmd(id, "buy");
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
    if (!Administrator(id)) return;
    ClearCustomInput(id);
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
    g_MenuUserid[id] = get_user_userid(id);
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
    menu_additem(menu, "持久玩法设置（JSON / 可反复开关）");
    // Nine management actions retain the original eight item indices.
    menu_setprop(menu, MPROP_PERPAGE, 0);
    menu_setprop(menu, MPROP_EXIT, MEXIT_FORCE);
    menu_setprop(menu, MPROP_EXITNAME, "关闭");
    menu_display(id, menu);
}

public FinishMenuHandler(id, menu, item)
{
    if (id < 1 || id >= sizeof g_Menu || g_Menu[id] != menu) return PLUGIN_HANDLED;
    new mapId = g_MenuMap[id], userid = g_MenuUserid[id];
    g_Menu[id] = -1;
    menu_destroy(menu);
    // Menu access can change while the menu is open. Never trust the earlier
    // command check or let a disconnected client/other mode submit a change.
    if (item < 0 || !Administrator(id, userid))
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
        case 8:
        {
            ShowCustomSettings(id);
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
    if (!Administrator(id)) return;
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
    g_MenuUserid[id] = get_user_userid(id);
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
    new mapId = g_MenuMap[id], userid = g_MenuUserid[id];
    g_Menu[id] = -1;
    menu_destroy(menu);
    if (!hasItem || !Administrator(id, userid))
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

ClearCustomInput(id)
{
    g_InputUserid[id] = 0;
    g_InputRevision[id] = 0;
    g_InputSetting[id] = -1;
    g_InputKey[id][0] = 0;
}

ClearCustomPending(id)
{
    g_PendingUserid[id] = 0;
    g_PendingRevision[id] = 0;
    remove_task(TASK_CUSTOM_SAVE + id);
}

bool:CustomInfo(&revision, &count, &busy, &result, error[], length)
{
    return csol_custom_info(revision, count, busy, result, error, length) &&
        revision > 0 && count > 0 && count <= 128;
}

CustomName(index, const key[], output[], length)
{
    if (index >= 0 && index < sizeof g_CustomKeys && equal(key, g_CustomKeys[index]))
        copy(output, length, g_CustomNames[index]);
    else copy(output, min(length, 24), key);
}

CustomExplain(id, const key[], const comment[])
{
    console_print(id, "[大灾变] %s", key);
    new offset, total = strlen(comment), line[128];
    while (offset < total)
    {
        new length = min(total - offset, 120);
        // The provider validates UTF8. End every console message at a complete
        // codepoint; long explanations never occupy the native menu buffer.
        while (offset + length < total && length > 0 && (comment[offset + length] & 0xC0) == 0x80) length--;
        if (!length) break;
        copy(line, length, comment[offset]);
        console_print(id, "%s", line);
        offset += length;
    }
}

ShowCustomSettings(id)
{
    if (!Administrator(id)) return;
    ClearCustomInput(id);
    new revision, count, busy, result, error[256];
    if (!CustomInfo(revision, count, busy, result, error, charsmax(error)))
    {
        client_print(id, print_chat, "[大灾变] 持久配置尚未就绪：%s", error);
        return;
    }
    new userid = get_user_userid(id);
    CloseFinishMenu(id);
    new title[128];
    formatex(title, charsmax(title), "持久玩法设置  修订 %d^n%s  *换图生效", revision,
        busy ? "正在保存" : "点击编辑");
    new menu = menu_create(title, "CustomSettingsHandler");
    g_Menu[id] = menu;
    g_MenuUserid[id] = userid;
    g_MenuRevision[id] = revision;
    new key[64], comment[512], type, nextMap, available, Float:value, Float:minimum, Float:maximum;
    new label[96], name[40], number[12];
    for (new index; index < count; index++)
    {
        if (!csol_custom_read(index, key, charsmax(key), comment, charsmax(comment), type, value, minimum, maximum, nextMap, available) ||
            !Administrator(id, userid))
        {
            CloseFinishMenu(id);
            return;
        }
        CustomName(index, key, name, charsmax(name));
        if (type == CSOL_CUSTOM_BOOLEAN)
            formatex(label, charsmax(label), "[%s] %s%s", value == 0.0 ? "关闭" : "开启", name, nextMap ? " *" : "");
        else if (available)
            formatex(label, charsmax(label), "%s：%.3f%s", name, value, nextMap ? " *" : "");
        else formatex(label, charsmax(label), "%s：未设置%s", name, nextMap ? " *" : "");
        num_to_str(index, number, charsmax(number));
        menu_additem(menu, label, number);
    }
    new finalRevision, finalCount, finalBusy, finalResult;
    if (!CustomInfo(finalRevision, finalCount, finalBusy, finalResult, error, charsmax(error)) ||
        finalRevision != revision || finalCount != count || !Administrator(id, userid))
    {
        CloseFinishMenu(id);
        client_print(id, print_chat, "[大灾变] 设置已更新，请重新打开持久设置页。");
        return;
    }
    menu_setprop(menu, MPROP_EXITNAME, "返回管理");
    menu_setprop(menu, MPROP_PERPAGE, 5);
    menu_setprop(menu, MPROP_BACKNAME, "上一页");
    menu_setprop(menu, MPROP_NEXTNAME, "下一页");
    menu_display(id, menu);
}

public CustomSettingsHandler(id, menu, item)
{
    if (id < 1 || id > 32 || g_Menu[id] != menu) return PLUGIN_HANDLED;
    new userid = g_MenuUserid[id], revision = g_MenuRevision[id];
    new number[12], access, callback;
    new bool:hasItem = item >= 0 && menu_item_getinfo(menu, item, access, number, charsmax(number), _, _, callback);
    g_Menu[id] = -1;
    menu_destroy(menu);
    if (!Administrator(id, userid)) return PLUGIN_HANDLED;
    if (!hasItem)
    {
        ShowFinishMenu(id);
        return PLUGIN_HANDLED;
    }
    ShowCustomEntry(id, str_to_num(number), revision);
    return PLUGIN_HANDLED;
}

ShowCustomEntry(id, index, expectedRevision)
{
    if (!Administrator(id)) return;
    new revision, count, busy, result, error[256];
    if (!CustomInfo(revision, count, busy, result, error, charsmax(error))) return;
    if (revision != expectedRevision || index < 0 || index >= count)
    {
        client_print(id, print_chat, "[大灾变] 菜单已过时，已刷新当前设置。");
        ShowCustomSettings(id);
        return;
    }
    new key[64], comment[512], type, nextMap, available, Float:value, Float:minimum, Float:maximum;
    new userid = get_user_userid(id);
    if (!csol_custom_read(index, key, charsmax(key), comment, charsmax(comment), type, value, minimum, maximum, nextMap, available) ||
        !Administrator(id, userid)) return;
    CustomExplain(id, key, comment);
    if (type == CSOL_CUSTOM_BOOLEAN)
    {
        SubmitCustomEdit(id, userid, revision, index, value == 0.0 ? 1.0 : 0.0);
        return;
    }
    if (type != CSOL_CUSTOM_INTEGER && type != CSOL_CUSTOM_REAL) return;
    new title[192], current[32], name[40];
    CustomName(index, key, name, charsmax(name));
    if (available) formatex(current, charsmax(current), "%.6f", value);
    else copy(current, charsmax(current), "尚未设置");
    formatex(title, charsmax(title), "%s^n当前：%s^n范围：%.3f..%.3f^n%s^n详细说明已输出到控制台", name, current, minimum, maximum,
        nextMap ? "*换图后生效" : "保存后生效");
    CloseFinishMenu(id);
    new menu = menu_create(title, "CustomEntryHandler");
    g_Menu[id] = menu;
    g_MenuUserid[id] = userid;
    g_MenuRevision[id] = revision;
    new number[12]; num_to_str(index, number, charsmax(number));
    menu_additem(menu, type == CSOL_CUSTOM_INTEGER ? "输入整数" : "输入数值（普通小数）", number);
    menu_setprop(menu, MPROP_EXITNAME, "返回设置");
    menu_display(id, menu);
}

public CustomEntryHandler(id, menu, item)
{
    if (id < 1 || id > 32 || g_Menu[id] != menu) return PLUGIN_HANDLED;
    new userid = g_MenuUserid[id], revision = g_MenuRevision[id];
    new number[12], access, callback;
    new bool:hasItem = item >= 0 && menu_item_getinfo(menu, item, access, number, charsmax(number), _, _, callback);
    g_Menu[id] = -1;
    menu_destroy(menu);
    if (!Administrator(id, userid)) return PLUGIN_HANDLED;
    if (!hasItem) { ShowCustomSettings(id); return PLUGIN_HANDLED; }
    new currentRevision, count, busy, result, error[256], index = str_to_num(number);
    if (!CustomInfo(currentRevision, count, busy, result, error, charsmax(error)) || currentRevision != revision ||
        index < 0 || index >= count || busy)
    {
        client_print(id, print_chat, "[大灾变] 当前设置正在保存或菜单已过时，请重新选择。");
        ShowCustomSettings(id);
        return PLUGIN_HANDLED;
    }
    new key[64], comment[512], type, nextMap, available, Float:value, Float:minimum, Float:maximum;
    if (!csol_custom_read(index, key, charsmax(key), comment, charsmax(comment), type, value, minimum, maximum, nextMap, available) ||
        !Administrator(id, userid) || (type != CSOL_CUSTOM_INTEGER && type != CSOL_CUSTOM_REAL)) return PLUGIN_HANDLED;
    g_InputUserid[id] = userid;
    g_InputRevision[id] = revision;
    g_InputSetting[id] = index;
    g_InputType[id] = type;
    copy(g_InputKey[id], charsmax(g_InputKey[]), key);
    client_print(id, print_chat, "[大灾变] 输入 %s 的%s；提交时会重新核对权限和修订。", key,
        type == CSOL_CUSTOM_INTEGER ? "整数" : "普通小数");
    // Fixed command token, never a user key/value interpolated as commands.
    client_cmd(id, "messagemode csol_custom_value");
    return PLUGIN_HANDLED;
}

bool:CustomNumber(const text[], type, &Float:value)
{
    new length = strlen(text), digits, dots, integer;
    if (!length || length > 31) return false;
    for (new i; i < length; i++)
    {
        new c = text[i];
        if (c == '.' && type == CSOL_CUSTOM_REAL && !dots && i && i + 1 < length) { dots++; continue; }
        if (c < '0' || c > '9') return false;
        digits++;
        if (type == CSOL_CUSTOM_INTEGER)
        {
            new digit = c - '0';
            if (integer > (2147483647 - digit) / 10) return false;
            integer = integer * 10 + digit;
        }
    }
    if (!digits) return false;
    value = type == CSOL_CUSTOM_INTEGER ? float(integer) : str_to_float(text);
    if ((_:value & 0x7f800000) == 0x7f800000) return false;
    // The native ABI carries Float. Never silently round a typed integer.
    if (type == CSOL_CUSTOM_INTEGER && (value >= 2147483648.0 || floatround(value) != integer)) return false;
    return true;
}

public CommandCustomValue(id)
{
    if (id < 1 || id > 32) return PLUGIN_HANDLED;
    new userid = g_InputUserid[id], revision = g_InputRevision[id], index = g_InputSetting[id], type = g_InputType[id];
    new expectedKey[64]; copy(expectedKey, charsmax(expectedKey), g_InputKey[id]);
    ClearCustomInput(id);
    if (!userid || !Administrator(id, userid)) return PLUGIN_HANDLED;
    new text[128]; read_args(text, charsmax(text)); remove_quotes(text); trim(text);
    new Float:value;
    if (!CustomNumber(text, type, value))
    {
        client_print(id, print_chat, "[大灾变] 数值无效：只允许非负整数/普通小数；整数不可超范围或被Float舍入。");
        ShowCustomSettings(id);
        return PLUGIN_HANDLED;
    }
    new currentRevision, count, busy, result, error[256];
    if (!CustomInfo(currentRevision, count, busy, result, error, charsmax(error)) || currentRevision != revision ||
        index < 0 || index >= count || busy)
    {
        client_print(id, print_chat, "[大灾变] 输入已过时或服务器正在保存，没有提交更改。");
        ShowCustomSettings(id);
        return PLUGIN_HANDLED;
    }
    new key[64], comment[512], currentType, nextMap, available, Float:current, Float:minimum, Float:maximum;
    if (!csol_custom_read(index, key, charsmax(key), comment, charsmax(comment), currentType, current, minimum, maximum, nextMap, available) ||
        !equal(key, expectedKey) || currentType != type || !Administrator(id, userid)) return PLUGIN_HANDLED;
    if (!(value >= minimum && value <= maximum))
    {
        client_print(id, print_chat, "[大灾变] 数值不在当前允许范围，没有提交更改。");
        ShowCustomSettings(id);
        return PLUGIN_HANDLED;
    }
    SubmitCustomEdit(id, userid, revision, index, value);
    return PLUGIN_HANDLED;
}

SubmitCustomEdit(id, userid, revision, index, Float:value)
{
    if (!Administrator(id, userid)) return;
    new result = csol_custom_edit(id, userid, revision, index, value);
    if (result == CSOL_CUSTOM_QUEUED || result == CSOL_CUSTOM_SAVED)
    {
        ClearCustomPending(id);
        g_PendingUserid[id] = userid; g_PendingRevision[id] = revision;
        g_PendingSetting[id] = index; g_PendingValue[id] = value; g_PendingPolls[id] = 0;
        client_print(id, print_chat, "[大灾变] %s，等待JSON落盘结果与修订核实；目前尚未确认永久保存。",
            result == CSOL_CUSTOM_QUEUED ? "更改已排队" : "服务已返回结果");
        set_task(0.25, "PollCustomSave", TASK_CUSTOM_SAVE + id);
    }
    else
    {
        new currentRevision, count, busy, status, error[256];
        CustomInfo(currentRevision, count, busy, status, error, charsmax(error));
        client_print(id, print_chat, "[大灾变] 更改未提交（%d）：%s", result, error);
    }
    if (Administrator(id, userid)) ShowCustomSettings(id);
}

public PollCustomSave(task)
{
    new id = task - TASK_CUSTOM_SAVE;
    if (id < 1 || id > 32) return;
    new userid = g_PendingUserid[id];
    if (!userid || !Administrator(id, userid)) { ClearCustomPending(id); return; }
    new revision, count, busy, result, error[256];
    if (!CustomInfo(revision, count, busy, result, error, charsmax(error)))
    {
        ClearCustomPending(id);
        client_print(id, print_chat, "[大灾变] 保存状态不可用，请重新打开设置核实：%s", error);
        return;
    }
    if (busy && ++g_PendingPolls[id] <= 80)
    {
        set_task(0.25, "PollCustomSave", TASK_CUSTOM_SAVE + id);
        return;
    }
    new expectedRevision = g_PendingRevision[id], index = g_PendingSetting[id];
    new Float:expectedValue = g_PendingValue[id];
    ClearCustomPending(id);
    if (busy)
    {
        client_print(id, print_chat, "[大灾变] 后台仍在处理，尚不能确认永久保存；请稍后重新查看。");
        return;
    }
    new key[64], comment[512], type, nextMap, available, Float:value, Float:minimum, Float:maximum;
    if (result == CSOL_CUSTOM_SAVED && revision == expectedRevision + 1 && index >= 0 && index < count &&
        csol_custom_read(index, key, charsmax(key), comment, charsmax(comment), type, value, minimum, maximum, nextMap, available) &&
        available && value == expectedValue && Administrator(id, userid))
    {
        client_print(id, print_chat, "[大灾变] %s 已永久保存到JSON，修订 %d。%s", key, revision,
            nextMap ? "下次本图会话生效。" : "配套消费者将应用该值。");
    }
    else client_print(id, print_chat, "[大灾变] 未确认本次更改已保存，请刷新设置核实：%s", error);
}
