// SPDX-License-Identifier: GPL-3.0-or-later
#define CSOL_EMERGENCY_FIXTURE_ONLY
#include "../csol-emergency-kick/sdk.cpp"
#undef CSOL_EMERGENCY_FIXTURE_ONLY

namespace phase_server
{
using namespace kick_test;
void Test(bool compatible, bool died)
{
    emergency_integration::Settings(false);
    Case c;
    if (compatible)
    {
        g_engfuncs.pfnInfoKeyValue = [](char*, const char* key) -> char*
        { return const_cast<char*>(!std::strcmp(key,"csol_protocol") || !std::strcmp(key,"csol_kick_phase") ? "1" : ""); };
        CSOL_ResetHumanKick(c.player);
        Require(c.Update(), "actual profile rebind reads the explicit new client capability");
    }
    c.player.pev->weaponanim = 3;
    c.packets.clear(); c.weaponAnims = 0;
    bool captured{};
    const auto message = c.f.engine.messages.at(kick::MessageName);
    c.callback = [&](std::string_view event)
    {
        if (event == "message" && c.pending.id == message)
        {
            const auto packet = kick::Decode(c.packets.back().bytes);
            if (packet && (packet->flags & kick::Active))
            {
                captured = true;
                Expect(c.weaponAnims == 0, "client captures its old animation before the first foot SVC reset");
            }
        }
    };
    Require(c.Try(), "actual kick starts under capability negotiation");
    Expect(captured, "authoritative Active snapshot is delivered before animation replacement");
    c.callback = {};
    c.AdvanceAnimation(.8f);
    if (died) { c.player.pev->health = 0; c.player.pev->deadflag = DEAD_DEAD; }
    Require(c.Update(), "actual completed sequence retires the kick");
    const auto packet = c.Snapshot();
    Require(bool(packet), "completed state is decodable with unchanged ten-byte framing");
    unsigned resumed{};
    for (const auto& message : c.packets)
        if (message.id == c.f.engine.messages.at(kick::MessageName))
            if (const auto value = kick::Decode(message.bytes); value && (value->flags & kick::ResumePhase)) ++resumed;
    Expect(resumed == unsigned(compatible && !died),
        "resume acknowledgement requires negotiated capability and a living restored owner");
    Expect(!(packet->flags & kick::ResumePhase),
        "even a rolled-back client with stale userinfo receives a plain legacy completion snapshot");
    Expect(!(packet->flags & kick::Active), "completion never combines Active and Resume");
    if (!died)
        Expect(c.player.pev->weaponanim == 3 && c.deploys == 0,
            "server restores presentation without Deploy or restarting a weapon action");
    const auto count = c.packets.size();
    Require(c.Update(), "repeated update is valid");
    Expect(c.packets.size() == count, "one-shot Resume does not cause an extra next-frame message");
}
void InfoCancellation()
{
    emergency_integration::Settings(false);
    Case c;
    const auto read = g_engfuncs.pfnGetInfoKeyBuffer;
    static Case* current{}; static int keyReads{};
    current = &c; keyReads = 0;
    g_engfuncs.pfnGetInfoKeyBuffer = [](edict_t*) -> char*
    { current->f.runtime->RequestClose(); return const_cast<char*>("controlled-userinfo"); };
    g_engfuncs.pfnInfoKeyValue = [](char*, const char*) -> char*
    { ++keyReads; return const_cast<char*>("1"); };
    Expect(!c.Try() && keyReads == 0,
        "retirement inside the info-buffer callback prevents the next engine read and animation mutation");
    g_engfuncs.pfnGetInfoKeyBuffer = read;
    current = nullptr;
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]).starts_with("phase-"))
    {
        try
        {
            const std::string_view name(argv[1]);
            if (name == "phase-new") phase_server::Test(true,false);
            else if (name == "phase-legacy") phase_server::Test(false,false);
            else if (name == "phase-dead") phase_server::Test(true,true);
            else if (name == "phase-info-cancel") phase_server::InfoCancellation();
            else throw std::runtime_error("unknown phase group");
        }
        catch (const std::exception& error) { ++sdk_test::failures; std::cerr << "ERROR " << error.what() << '\n'; }
        std::cout << "behaviorChecks=" << kick_test::behaviorChecks << " checks=" << sdk_test::checks
            << " failures=" << sdk_test::failures << '\n';
        return sdk_test::failures ? 1 : 0;
    }
    return CsolEmergencyTestMain(argc,argv);
}
