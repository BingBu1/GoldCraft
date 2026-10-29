// SPDX-License-Identifier: GPL-3.0-or-later
// Source prepared only: execution must be scheduled with the coordinator.
#include "../../native/csol/client/damage_numbers.hpp"
#include "../../native/csol/server/damage_numbers.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>

namespace
{
using namespace csol::damage;
int checks{};
void Check(bool condition, const std::source_location at = std::source_location::current())
{
    ++checks;
    if (!condition) throw std::runtime_error("damage-number check at line " + std::to_string(at.line()));
}
bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
constexpr Animation skinOne{0.8f, 0.28f, 100};
constexpr Message hit{9, 0, 0, 30};

void Wire()
{
    // Literal record from Test2.dem, not an encode/decode-only round trip.
    constexpr std::array<std::uint8_t, 9> wire{0xce, 9, 0, 0, 0, 0x4f, 1, 0, 0};
    Message message{77, 6, 4, -10};
    Check(Decode(std::as_bytes(std::span(wire)), message) == ParseResult::Number);
    Check(message == Message{9, 0, 0, 335});
    Check(CSOLDamageNumberBytes({9, 0, 0, 335}) == wire);
    const Message before = message;
    for (std::size_t size = 1; size < wire.size(); ++size)
    {
        Check(Decode(std::as_bytes(std::span(wire)).first(size), message) == ParseResult::Malformed);
        Check(message == before);
    }
    std::array<std::byte, 10> trailing{};
    trailing[0] = std::byte{206};
    Check(Decode(trailing, message) == ParseResult::Malformed && message == before);
    for (const auto opcode : {177, 199, 40})
    {
        const std::array other{std::byte(opcode)};
        Check(Decode(other, message) == ParseResult::Unrelated && message == before);
    }
    Check(Decode({}, message) == ParseResult::Unrelated);
    constexpr std::array<std::uint8_t, 9> signedWire{206, 0xff, 0xff, 0xfe, 0x80, 0, 0, 0, 0x80};
    Check(Decode(std::as_bytes(std::span(signedWire)), message) == ParseResult::Number);
    Check(message.target == -1 && message.style == 254 && message.flags == 128 &&
        message.value == std::numeric_limits<std::int32_t>::min());
}

void Conversion()
{
    Check(CSOLTruncateDamageNumber(30.99f) == 30);
    Check(CSOLTruncateDamageNumber(0.99f) == 0);
    Check(CSOLTruncateDamageNumber(-30.99f) == -30);
    Check(CSOLTruncateDamageNumber(-2147483648.0f) == std::numeric_limits<std::int32_t>::min());
    Check(CSOLTruncateDamageNumber(std::nextafter(2147483648.0f, 0.0f)) == 2147483520);
    Check(!CSOLTruncateDamageNumber(2147483648.0f));
    Check(!CSOLTruncateDamageNumber(std::nextafter(-2147483648.0f, -INFINITY)));
    Check(!CSOLTruncateDamageNumber(std::numeric_limits<float>::quiet_NaN()));
    Check(!CSOLTruncateDamageNumber(std::numeric_limits<float>::infinity()));
}

void OriginalPanelLifetime()
{
    Numbers playerA, playerB;
    for (int i = 0; i < 12; ++i)
    {
        // Original client entry appends each accepted display message. This
        // does not require the server to send one message per physical hit.
        Check(playerA.Push(hit, {float(i), 0, 0}, skinOne, 10));
        Check(playerB.Push(hit, {0, float(i), 0}, skinOne, 10));
    }
    Check(playerA.Entries().size() == 12 && playerB.Entries().size() == 12);
    Check(playerA.Push({9, 4, 3, 100}, {12, 0, 0}, skinOne, 10.1));
    Check(playerA.Entries().size() == 13 && playerA.Entries().front().anchor.x == 0);
    Check(playerA.Entries().back().message.value == 100);
    Check(playerB.Entries().size() == 12 && playerB.Entries().front().anchor.y == 0);
    playerA.Advance(10.2);
    Check(playerA.Entries().size() == 13); // No eviction on the sixth message.
    playerA.Advance(10.73);
    Check(playerA.Entries().size() == 1 && playerA.Entries()[0].message.value == 100);
    playerA.Reset();
    Check(playerA.Entries().empty() && playerB.Entries().size() == 12);
    Check(playerA.Push({32767, 6, 4, std::numeric_limits<std::int32_t>::min()}, {}, skinOne, 0));
    Check(playerA.Entries()[0].Text() == "-2147483648");
    const auto size = playerA.Entries().size();
    Check(!playerA.Push({-1, 0, 0, 10}, {}, skinOne, 0));
    Check(!playerA.Push({1, 7, 0, 10}, {}, skinOne, 0));
    Check(!playerA.Push(hit, {NAN, 0, 0}, skinOne, 0));
    Check(!playerA.Push(hit, {}, {0, 0, 100}, 0));
    Check(!playerA.Push(hit, {}, skinOne, NAN));
    Check(playerA.Entries().size() == size && playerA.Entries()[0].newest);
}

void AnimationTiming()
{
    Numbers numbers;
    Check(numbers.Push(hit, {1, 2, 3}, skinOne, 0));
    numbers.Advance(0.1);
    Check(numbers.Push(hit, {4, 5, 6}, skinOne, 0.15));
    Check(Near(numbers.Entries()[0].effectiveAge, 0.1f));
    numbers.Advance(0.2);
    Check(Near(numbers.Entries()[0].effectiveAge, 0.28f));
    Check(Near(numbers.Entries()[1].effectiveAge, 0.05f));
    numbers.Advance(0.4);
    Check(Near(numbers.Entries()[0].effectiveAge, 0.48f));
    Check(numbers.Entries()[0].anchor == Anchor{1, 2, 3});
    numbers.Advance(0.3); // Small host correction cannot rewind a drawn panel.
    Check(Near(numbers.Entries()[0].effectiveAge, 0.48f));
    numbers.Advance(NAN);
    Check(numbers.Entries().size() == 2);
    numbers.Advance(0.73);
    Check(numbers.Entries().size() == 1 && numbers.Entries()[0].anchor == Anchor{4, 5, 6});
    numbers.Advance(1);
    Check(numbers.Entries().empty());

    numbers.Reset();
    Check(numbers.Push(hit, {}, skinOne, 0));
    numbers.Advance(0.5);
    Check(numbers.Push(hit, {}, skinOne, 0.5));
    numbers.Advance(0.6);
    Check(Near(numbers.Entries()[0].effectiveAge, 0.6f)); // Already past skip: no acceleration.
    numbers.Reset();
    for (int i = 0; i < 8; ++i) Check(numbers.Push(hit, {}, skinOne, 0));
    numbers.Advance(0.05);
    Check(numbers.Entries().size() == 8);
    for (std::size_t i = 0; i < 7; ++i) Check(Near(numbers.Entries()[i].effectiveAge, 0.28f));
    Check(Near(numbers.Entries()[7].effectiveAge, 0.05f));
}

void ImageAnimation()
{
    const auto measured = MeasureDigits(410, 51);
    Check(measured && Near(measured->glyphWidth, 32.83203125f) && measured->glyphHeight == 51);
    Check(!MeasureDigits(9, 51) && !MeasureDigits(410, 0) && !MeasureDigits(0xffffffff, 51));
    Numbers numbers;
    Check(numbers.Push(hit, {}, skinOne, 0));
    const ImageStyle style{*measured, 1.5f, 0.375f, 1.2f};
    numbers.Advance(0);
    auto sample = Sample(numbers.Entries()[0], style, {});
    Check(sample && sample->width == 49 && sample->height == 76 && sample->alpha == 255);
    numbers.Advance(0.54);
    sample = Sample(numbers.Entries()[0], style, {});
    Check(sample && sample->alpha >= 127 && sample->alpha <= 128 && Near(sample->rise, 54));
    sample = Sample(numbers.Entries()[0], style, {10000, 0, 0});
    Check(sample && sample->width == 12 && sample->height == 19); // Independent truncation.
    Check(!Sample(numbers.Entries()[0], style, {NAN, 0, 0}));
    Check(!Sample(numbers.Entries()[0], {{0, 51}, 1, 1, 1}, {}));
    Check(!Sample(numbers.Entries()[0], {*measured, INFINITY, 1, 1}, {}));
    Number tiny = numbers.Entries()[0];
    tiny.animation = {0.0005f, 0, 100};
    tiny.effectiveAge = 0;
    sample = Sample(tiny, style, {});
    Check(sample && sample->alpha == 0);
}
}

int main()
{
    try
    {
        Wire(); Conversion(); OriginalPanelLifetime(); AnimationTiming(); ImageAnimation();
        std::cout << checks << " checks passed; no host-hook or visual acceptance\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
