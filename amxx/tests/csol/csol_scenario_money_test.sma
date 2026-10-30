// SPDX-License-Identifier: GPL-3.0-or-later
// Temporary native-account regression probe. Never install as a production plugin.
#include <amxmodx>
#include <reapi>

new g_Enabled, g_Target, g_Userid, g_Checks, g_Failed;
new g_Messages, g_Balance, g_Track;

public plugin_init()
{
    register_plugin("CSOL scenario money checks", "0.1.0", "GoldCraft contributors");
    g_Enabled = create_cvar("csol_money_test", "0");
    register_srvcmd("csol_money_status", "Status");
    register_srvcmd("csol_money_check", "CheckAccount");
    register_message(get_user_msgid("Money"), "MoneyMessage");
    RegisterHookChain(RG_CBasePlayer_AddAccount, "AccountChanged", true);
}

bool:Current()
{
    return g_Target >= 1 && g_Target <= MaxClients && is_user_connected(g_Target) &&
        is_user_bot(g_Target) && get_user_userid(g_Target) == g_Userid;
}

Check(bool:condition, const name[])
{
    g_Checks++;
    if (!condition) g_Failed++;
    server_print("[CSOL money] check=%s result=%s", name, condition ? "PASS" : "FAIL");
}

public MoneyMessage(msgid, destination, recipient)
{
    if (!get_pcvar_num(g_Enabled) || recipient != g_Target || !Current()) return PLUGIN_CONTINUE;
    g_Messages++;
    if (get_msg_args() != 2) { g_Balance = -1; return PLUGIN_CONTINUE; }
    g_Balance = get_msg_arg_int(1);
    g_Track = get_msg_arg_int(2);
    return PLUGIN_CONTINUE;
}

public AccountChanged(index, amount, RewardType:type, bool:tracked)
{
    if (!get_pcvar_num(g_Enabled) || !is_user_connected(index)) return;
    server_print("[CSOL money] event=account slot=%d userid=%d amount=%d reason=%d tracked=%d balance=%d",
        index, get_user_userid(index), amount, type, tracked, get_member(index, m_iAccount));
}

public Status()
{
    if (!get_pcvar_num(g_Enabled)) return PLUGIN_HANDLED;
    server_print("[CSOL money] mode=%d native_start=%d native_max=%d",
        get_cvar_num("csol_game_mode"), get_cvar_num("mp_startmoney"), get_cvar_num("mp_maxmoney"));
    for (new index = 1; index <= MaxClients; index++)
    {
        if (!is_user_connected(index)) continue;
        server_print("[CSOL money] slot=%d userid=%d bot=%d team=%d alive=%d balance=%d",
            index, get_user_userid(index), is_user_bot(index), get_member(index, m_iTeam),
            is_user_alive(index), get_member(index, m_iAccount));
    }
    return PLUGIN_HANDLED;
}

bool:Apply(const name[], amount, AccountSet:operation, expected, bool:tracked)
{
    if (!Current()) return false;
    g_Messages = 0; g_Balance = -1; g_Track = -1;
    rg_add_account(g_Target, amount, operation, tracked);
    Check(Current(), "same_connection_after_native");
    if (!Current()) return false;
    Check(get_member(g_Target, m_iAccount) == expected, name);
    Check(g_Messages == 1 && g_Balance == expected && g_Track == _:tracked, "single_native_money_packet");
    return true;
}

public CheckAccount()
{
    if (!get_pcvar_num(g_Enabled) || get_cvar_num("csol_game_mode") != 2 ||
        read_argc() != 2 || g_Target) return PLUGIN_HANDLED;
    new argument[12]; read_argv(1, argument, charsmax(argument));
    new index = str_to_num(argument);
    if (index < 1 || index > MaxClients || !is_user_connected(index) || !is_user_bot(index))
    {
        server_print("[CSOL money] refused: select a connected test bot");
        return PLUGIN_HANDLED;
    }
    g_Target = index; g_Userid = get_user_userid(index); g_Checks = 0; g_Failed = 0;
    new saved = get_member(index, m_iAccount);
    new start = get_cvar_num("mp_startmoney"), maximum = get_cvar_num("mp_maxmoney");
    do
    {
        if (!Apply("set_initial", 7500, AS_SET, 7500, false)) break;
        if (!Apply("native_purchase_debit", -6000, AS_ADD, 1500, true)) break;
        if (!Apply("set_near_cap", 31900, AS_SET, 31900, false)) break;
        if (!Apply("reward_cap", 500, AS_ADD, 32000, true)) break;
        if (!Apply("reward_at_cap", 500, AS_ADD, 32000, true)) break;
        if (!Apply("api_set_cap", 40000, AS_SET, 32000, false)) break;
        if (!Apply("positive_overflow", 2147483647, AS_ADD, 32000, true)) break;
        if (!Apply("negative_clamp", (-2147483647 - 1), AS_ADD, 0, true)) break;
        Apply("api_set_negative", -1, AS_SET, 0, false);
    } while (false);
    if (Current()) Apply("restore_account", saved, AS_SET, clamp(saved, 0, 32000), false);
    Check(start == get_cvar_num("mp_startmoney") && maximum == get_cvar_num("mp_maxmoney"), "native_cvars_unchanged");
    server_print("[CSOL money] result checks=%d failed=%d slot=%d userid=%d restored=%d",
        g_Checks, g_Failed, index, g_Userid, Current());
    g_Target = 0; g_Userid = 0;
    return PLUGIN_HANDLED;
}
