// SPDX-License-Identifier: GPL-3.0-or-later
#include <amxmodx>
#include <csol_game_mode>

#define MAX_MODE_RULES 128
#define MAX_PLUGIN_NAME 64

new g_Patterns[MAX_MODE_RULES][MAX_PLUGIN_NAME];
new g_Masks[MAX_MODE_RULES];
new g_RuleCount;
new bool:g_Scenario;
new bool:g_RestoringBots;
new g_BotCvars[3];
new g_BotPrevious[3][32];
new const g_BotNames[][] = { "sypb_auto_players", "sypb_quota_save", "sypb_quota" };
new const g_BotDisabled[] = { -1, -1, 0 };

public plugin_precache()
{
    new filename[MAX_PLUGIN_NAME];
    get_plugin(0, filename, charsmax(filename));
    if (!equal(filename, "goldcraft_game_mode.amxx"))
    {
        set_fail_state("模式门必须是主 plugins.ini 的第一个插件");
        return;
    }
    new mode = csol_game_mode_current();
    if (csol_game_mode_api_version() != 1 || mode < 0 || mode > 2)
    {
        set_fail_state("模式服务未就绪，csol_game_mode 只允许 0、1、2");
        return;
    }
    g_Scenario = mode == _:CSOL_MODE_SCENARIO;
    if (!ReadModePolicy())
    {
        set_fail_state("无法读取有效的 goldcraft/game_modes.ini");
        return;
    }
    new stopped;
    for (new index = 1, count = get_pluginsnum(); index < count; index++)
    {
        get_plugin(index, filename, charsmax(filename));
        if (PluginAllowed(filename, mode))
            continue;
        if (!pause("ac", filename))
        {
            set_fail_state("无法在预缓存前暂停插件：%s", filename);
            return;
        }
        stopped++;
    }
    if (!csol_game_mode_confirm(mode))
    {
        set_fail_state("模式隔离确认失败");
        return;
    }
    log_amx("本图模式 %d，已在预缓存前暂停 %d 个不适用插件", mode, stopped);
}

public plugin_init()
{
    register_plugin("GoldCraft 地图模式", "0.1.1", "GoldCraft contributors");
    IsolateScenarioBots();
}

public OnConfigsExecuted()
{
    // Server and SyPB configs can execute after plugin_init. Keep the native
    // Bot population manager out of scenario maps for their entire lifetime.
    IsolateScenarioBots();
}

IsolateScenarioBots()
{
    if (!g_Scenario) return;
    for (new i; i < sizeof g_BotCvars; i++)
    {
        if (!g_BotCvars[i])
        {
            g_BotCvars[i] = get_cvar_pointer(g_BotNames[i]);
            if (!g_BotCvars[i]) continue;
            get_pcvar_string(g_BotCvars[i], g_BotPrevious[i], charsmax(g_BotPrevious[]));
            hook_cvar_change(g_BotCvars[i], "ScenarioBotCvarChanged");
        }
        if (get_pcvar_num(g_BotCvars[i]) != g_BotDisabled[i])
            set_pcvar_num(g_BotCvars[i], g_BotDisabled[i]);
    }
}

public ScenarioBotCvarChanged(pcvar, const oldValue[], const newValue[])
{
    if (!g_Scenario || g_RestoringBots) return;
    for (new i; i < sizeof g_BotCvars; i++)
    {
        if (pcvar == g_BotCvars[i] && str_to_num(newValue) != g_BotDisabled[i])
        {
            set_pcvar_num(pcvar, g_BotDisabled[i]);
            return;
        }
    }
}

public plugin_end()
{
    // Restore the previous mode's policy; do not persist scenario-only values
    // to sypb.cfg or change the configured Bot population for native CS/ZP.
    g_RestoringBots = true;
    for (new i; i < sizeof g_BotCvars; i++)
        if (g_BotCvars[i]) set_pcvar_string(g_BotCvars[i], g_BotPrevious[i]);
}

bool:ReadModePolicy()
{
    new directory[192], path[256];
    get_localinfo("amxx_configsdir", directory, charsmax(directory));
    formatex(path, charsmax(path), "%s/goldcraft/game_modes.ini", directory);
    new file = fopen(path, "rt");
    if (!file) return false;
    new line[256], pattern[MAX_PLUGIN_NAME], mask[16], extra[16];
    new bool:valid = true;
    while (!feof(file))
    {
        fgets(file, line, charsmax(line));
        if (strlen(line) >= charsmax(line)) { valid = false; break; }
        trim(line);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;
        if (g_RuleCount >= MAX_MODE_RULES ||
            parse(line, pattern, charsmax(pattern), mask, charsmax(mask), extra, charsmax(extra)) != 2 ||
            strlen(pattern) >= charsmax(pattern) || !pattern[0] ||
            mask[0] < '0' || mask[0] > '7' || mask[1])
        { valid = false; break; }
        for (new i; pattern[i]; i++)
        {
            if (pattern[i] == '/' || pattern[i] == '\' ||
                (pattern[i] == '*' && (!i || pattern[i + 1])))
            { valid = false; break; }
        }
        if (!valid) break;
        copy(g_Patterns[g_RuleCount], charsmax(g_Patterns[]), pattern);
        g_Masks[g_RuleCount++] = mask[0] - '0';
    }
    fclose(file);
    return valid && g_RuleCount > 0;
}

bool:PluginAllowed(const filename[], mode)
{
    // Configuration can narrow the policy but cannot permit known ZP code to
    // run in native/scenario maps. Missing rules must not falsely confirm an
    // isolated map while objective_remover is still allowed to precache.
    if (mode != _:CSOL_MODE_ZOMBIE_PLAGUE &&
        (equali(filename, "zp50_", 5) || equali(filename, "goldcraft_zp", 12) ||
         equali(filename, "goldcraft_zombie_", 17) || equali(filename, "zombieplague", 12)))
        return false;
    if (mode == _:CSOL_MODE_SCENARIO && equali(filename, "nademodes.amxx"))
        return false;
    // First configuration match wins after mandatory restrictions. Unlisted
    // shared APIs retain normal behavior.
    for (new i; i < g_RuleCount; i++)
    {
        new length = strlen(g_Patterns[i]);
        if (g_Patterns[i][length - 1] == '*')
        {
            if (!equali(filename, g_Patterns[i], length - 1)) continue;
        }
        else if (!equali(filename, g_Patterns[i])) continue;
        return (g_Masks[i] & (1 << mode)) != 0;
    }
    return true;
}
