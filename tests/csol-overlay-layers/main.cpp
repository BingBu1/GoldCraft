// SPDX-License-Identifier: GPL-3.0-or-later
// Production overlay queue: immediate fallback, bottom-to-top compositor order,
// one paint per layer per frame, and no requeue into the frame being painted.
#include "../../native/csol/client/overlay_layers.hpp"
#include <cstdio>
#include <string>

using namespace csol::client;
namespace {
std::string trace;
unsigned checks{}, failures{};
void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
void Wall() { trace += "W"; }
void Style() { trace += "H"; }
void Skills() { trace += "S"; }
void Boss() { trace += "B"; }
void Buy() { trace += "M"; }
void Vote() { trace += "V"; }
void Requeue() { trace += "R"; QueueOverlayPaint(OverlayLayer::WallStatus, Wall); }
}

int main()
{
    // No compositor registered (tests, missing VGUI2Extension): paint at once.
    QueueOverlayPaint(OverlayLayer::BuyMenu, Buy);
    QueueOverlayPaint(OverlayLayer::HudStyle, Style);
    Check(trace == "MH", "inactive compositor keeps the old immediate paint");

    // VGUI2Extension calls larger altitudes first: vote35 buy31 skills22 style21 wall20.
    overlay_detail::active = true; trace.clear();
    QueueOverlayPaint(OverlayLayer::MapVote, Vote);
    QueueOverlayPaint(OverlayLayer::BuyMenu, Buy);
    QueueOverlayPaint(OverlayLayer::BossHealth, Boss);
    QueueOverlayPaint(OverlayLayer::CharacterSkills, Skills);
    QueueOverlayPaint(OverlayLayer::HudStyle, Style);
    QueueOverlayPaint(OverlayLayer::WallStatus, Wall);
    Check(trace.empty(), "active compositor defers every overlay");
    PaintQueuedOverlays();
    Check(trace == "WHSBMV", "compositor paints bottom to top: HUD, skills above panels, menus last");
    trace.clear(); PaintQueuedOverlays();
    Check(trace.empty(), "a frame's queue is consumed once");

    QueueOverlayPaint(OverlayLayer::HudStyle, Style);
    QueueOverlayPaint(OverlayLayer::HudStyle, Style);
    PaintQueuedOverlays();
    Check(trace == "H", "repeated queue in one frame paints once");

    trace.clear();
    QueueOverlayPaint(OverlayLayer::ScenarioAbilities, Requeue);
    PaintQueuedOverlays();
    Check(trace == "R", "requeue during painting waits for the next frame");
    trace.clear(); PaintQueuedOverlays();
    Check(trace == "W", "requeued paint runs next frame");

    QueueOverlayPaint(OverlayLayer::Count, Wall);
    QueueOverlayPaint(OverlayLayer::HudStyle, nullptr);
    trace.clear(); PaintQueuedOverlays();
    Check(trace.empty(), "invalid layer and null painter are ignored");
    Check(static_cast<int>(OverlayLayer::HudStyle) < static_cast<int>(OverlayLayer::CharacterSkills) &&
        static_cast<int>(OverlayLayer::CharacterSkills) < static_cast<int>(OverlayLayer::BuyMenu) &&
        static_cast<int>(OverlayLayer::BuyMenu) < static_cast<int>(OverlayLayer::MapVote), "declared layer order");
    std::printf("overlay layers: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
