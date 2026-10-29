// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "status_icons.hpp"

namespace csol::client
{
namespace
{
status::ProtectionIcons icons;
cvar_t* option{};
pfnUserMsgHook previous{};

int ReceiveStatus(const char* name, int size, void* data)
{
    SynchronizeSession();
    if (!data || size <= 0) return previous ? previous(name, size, data) : 1;
    status::Message message;
    const auto parsed = status::Decode({static_cast<const std::byte*>(data), std::size_t(size)}, message);
    if (parsed == status::ParseResult::Unrelated) return previous ? previous(name, size, data) : 1;
    // Do not pass malformed opcode40 data to an old unchecked decoder.
    if (parsed == status::ParseResult::Malformed) return 1;
    switch (message.operation)
    {
    case status::Operation::Add:
        if (message.style != 0 || message.type != 7) return previous ? previous(name, size, data) : 1;
        icons.Add(message, option ? option->value : 1.0f);
        return 1;
    case status::Operation::Remove:
        icons.Remove(message.id);
        break;
    case status::Operation::Clear:
        icons.Clear();
        break;
    }
    // Other types may belong to another consumer of the same message.
    return previous ? previous(name, size, data) : 1;
}
}

void InitializeStatusIcons()
{
    option = engine.pfnGetCvarPointer("cl_weaponparts_hud");
    if (!option) option = engine.pfnRegisterVariable("cl_weaponparts_hud", "1", FCVAR_ARCHIVE);
    // MetaHook replaces existing handlers; the engine registers missing ones.
    const auto old = api->HookUserMsg(status::MessageName, ReceiveStatus);
    if (old != ReceiveStatus) previous = old;
    if (!old) engine.pfnHookUserMsg(status::MessageName, ReceiveStatus);
}
void ResetStatusIcons()
{
    icons.ResetSession();
    ResetStatusIconGraphics();
}
void StatusIconsFrame()
{
    if (icons.VisibleCount()) PrepareStatusIconGraphics();
}
void StatusIconsVideoInit() { ResetStatusIconGraphics(); }
void DrawStatusIcons(float time)
{
    icons.Advance(time);
    if (!icons.VisibleCount() || engine.pfnGetCvarFloat("hud_draw") == 0) return;
    SCREENINFO screen{};
    screen.iSize = sizeof(screen);
    if (!engine.pfnGetScreenInfo(&screen)) return;
    DrawStatusIconGraphics(icons, screen.iWidth, screen.iHeight);
}
void ShutdownStatusIcons() { ResetStatusIcons(); }
}
