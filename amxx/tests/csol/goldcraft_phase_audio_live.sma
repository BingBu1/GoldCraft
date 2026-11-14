// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit isolated single-B validation. No automatic team/character selection,
// fabricated phase, shortened native preparation or synthetic audio message.
#include <amxmodx>
#include <reapi>

new g_Enable, g_Userid;
new g_Events[32][192], g_Count;

public plugin_init()
{
    register_plugin("CSOL single-B audio observer", "0.1.0", "GoldCraft contributors");
    g_Enable = create_cvar("gc_audio_live_test", "0");
    g_Userid = create_cvar("gc_audio_live_userid", "0");
    register_srvcmd("gc_audio_live_status", "Status");
    register_srvcmd("gc_audio_live_events", "Events");
    RegisterHookChain(RG_CBasePlayer_TakeDamage, "Damage", false);
    new message = get_user_msgid("CSOScnBgm");
    if (message) register_message(message, "Music");
    message = get_user_msgid("CsoTime");
    if (message) register_message(message, "Phase");
    message = get_user_msgid("CsoBreath");
    if (message) register_message(message, "Breath");
    message = get_user_msgid("CsoEnergy");
    if (message) register_message(message, "Stamina");
}
bool:Owned(id)
{
    return get_pcvar_num(g_Enable) && id > 0 && id <= MaxClients &&
        get_pcvar_num(g_Userid) > 0 && is_user_connected(id) && !is_user_bot(id) &&
        get_user_userid(id) == get_pcvar_num(g_Userid);
}
public Damage(id, inflictor, attacker, Float:damage, bits)
{
    if (!Owned(id)) return HC_CONTINUE;
    SetHookChainReturn(ATYPE_INTEGER, 0);
    return HC_SUPERCEDE;
}
public Status()
{
    for (new id = 1; id <= MaxClients; id++) if (Owned(id))
        server_print("[Audio live] player=%d userid=%d alive=%d health=%.1f protected=1", id,
            get_user_userid(id), is_user_alive(id), Float:get_entvar(id, var_health));
    return PLUGIN_HANDLED;
}
public Events()
{
    server_print("[Audio live] observed=%d retained=%d", g_Count, min(g_Count, sizeof(g_Events)));
    for (new event = max(0, g_Count - sizeof(g_Events)); event < g_Count; event++)
        server_print("%s", g_Events[event % sizeof(g_Events)]);
    return PLUGIN_HANDLED;
}
public Music(message, destination, recipient)
{
    if (!get_pcvar_num(g_Enable) || get_msg_args() < 7) return PLUGIN_CONTINUE;
    new paths[161], offset;
    for (new i = 7; i <= get_msg_args() && offset < charsmax(paths); i++)
    {
        new value = get_msg_arg_int(i);
        paths[offset++] = value ? value : '|';
    }
    formatex(g_Events[g_Count++ % sizeof(g_Events)], charsmax(g_Events[]),
        "[Audio live] t=%.3f bgm to=%d version=%d action=%d paths=%s", get_gametime(),
        recipient, get_msg_arg_int(1), get_msg_arg_int(2), paths);
    server_print("[Audio live] bgm destination=%d recipient=%d version=%d action=%d bytes=%d", destination,
        recipient, get_msg_arg_int(1), get_msg_arg_int(2), get_msg_args());
    return PLUGIN_CONTINUE;
}
public Phase(message, destination, recipient)
{
    if (!get_pcvar_num(g_Enable) || get_msg_args() != 19) return PLUGIN_CONTINUE;
    formatex(g_Events[g_Count++ % sizeof(g_Events)], charsmax(g_Events[]),
        "[Audio live] t=%.3f phase to=%d phase=%d preparing=%d", get_gametime(),
        recipient, get_msg_arg_int(2), get_msg_arg_int(3));
    server_print("[Audio live] phase destination=%d recipient=%d phase=%d preparing=%d", destination,
        recipient, get_msg_arg_int(2), get_msg_arg_int(3));
    return PLUGIN_CONTINUE;
}
public Breath(message, destination, recipient)
{
    if (!get_pcvar_num(g_Enable) || get_msg_args() != 14) return PLUGIN_CONTINUE;
    formatex(g_Events[g_Count++ % sizeof(g_Events)], charsmax(g_Events[]),
        "[Audio live] t=%.3f breath to=%d male=%d event=%d", get_gametime(),
        recipient, get_msg_arg_int(2), get_msg_arg_int(11));
    server_print("[Audio live] breath destination=%d recipient=%d male=%d event=%d", destination,
        recipient, get_msg_arg_int(2), get_msg_arg_int(11));
    return PLUGIN_CONTINUE;
}
public Stamina(message, destination, recipient)
{
    if (!get_pcvar_num(g_Enable) || get_msg_args() != 22) return PLUGIN_CONTINUE;
    new amount, maximum;
    for (new i = 0; i < 4; i++)
    {
        amount |= (get_msg_arg_int(15 + i) & 255) << (8 * i);
        maximum |= (get_msg_arg_int(19 + i) & 255) << (8 * i);
    }
    formatex(g_Events[g_Count++ % sizeof(g_Events)], charsmax(g_Events[]),
        "[Audio live] t=%.3f stamina to=%d flags=%d amount=%.3f maximum=%.3f", get_gametime(),
        recipient, get_msg_arg_int(2), Float:amount, Float:maximum);
    return PLUGIN_CONTINUE;
}
