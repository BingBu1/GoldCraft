// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../scenario_respawn_resources.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>

namespace csol::scenario_respawn_ui
{
enum class ParseResult { Unrelated, Malformed, Accepted };
enum class Kind { Round, Outcome, Consumable, RetryCountdown, RetryAnnouncement, RetryBudget };
struct RetryBudget
{
    std::int32_t remaining{-2}, initial{-2};
    bool Valid() const { return remaining >= -2 && initial >= -2; }
};
struct Message
{
    Kind kind{};
    unsigned index{};
    int count{-1};
    float cooldown{};
    RetryBudget budget;
};
inline constexpr std::string_view RetryBudgetTitleToken = "#CSO_ZBS_RetryCountText";
inline constexpr std::string_view RetryBudgetUnlimitedToken = "#CSO_ZBS_RetryCountTextunlimit";
inline std::wstring FormatRetryBudget(const RetryBudget& budget, std::wstring_view unlimited)
{
    // Original client02A05510 selects both sentinel forms from the second LONG.
    if (budget.initial == -2) return {};
    if (budget.initial == -1) return std::wstring(unlimited);
    return std::to_wstring(budget.remaining) + L"/" + std::to_wstring(budget.initial);
}
inline constexpr std::array<int, 4> ItemTops{1, 29, 57, 90};
inline constexpr std::array<int, 4> ItemY{273, 316, 359, 402};
inline constexpr unsigned GoldenSpawnedToken = 5, GoldenKilledToken = 6;
inline constexpr std::array<std::string_view, 7> CenterTokens{
    "#CSO_ZBS_AutoRespawnWait", "#CSO_ZBS_CantRespawnAnymore",
    "#CSO_ZBS_RespawnWait", "#CSO_ZBS_UseRetryItem", "#CSO_CongAllRoundClear",
    "#CSO_ZBS_SpawnedGoldenZB", "#CSO_ZBS_KilledGoldenZB"
};
inline unsigned Byte(std::byte value) { return std::to_integer<unsigned>(value); }
inline unsigned Word(std::span<const std::byte> b, unsigned at)
{ return Byte(b[at]) | (Byte(b[at + 1]) << 8); }

inline ParseResult DecodeMP(std::span<const std::byte> b, Message& out)
{
    if (b.empty() || (Byte(b[0]) != 25 && Byte(b[0]) != 27)) return ParseResult::Unrelated;
    if (b.size() < 2) return ParseResult::Malformed;
    Message m;
    if (Byte(b[0]) == 25)
    {
        if (Byte(b[1]) != 8 && Byte(b[1]) != 9) return ParseResult::Unrelated;
        if (b.size() != 2) return ParseResult::Malformed;
        m.kind = Kind::Outcome; m.index = Byte(b[1]) - 8;
    }
    else if (Byte(b[1]) == 3)
    {
        if (b.size() != 8) return ParseResult::Malformed;
        if (!std::isfinite(std::bit_cast<float>(Word(b, 4) | (Word(b, 6) << 16))))
            return ParseResult::Malformed;
        m.kind = Kind::Round;
    }
    else if (Byte(b[1]) == 4)
    {
        // Original 02A5AF60 calls READ_FLOAT at 02AA44A0: four bytes,
        // independently of the stock engine's two-byte WRITE_COORD format.
        if (b.size() != 9 || Byte(b[2]) > 3) return ParseResult::Malformed;
        m.kind = Kind::Consumable; m.index = Byte(b[2]);
        m.cooldown = std::bit_cast<float>(Word(b, 3) | (Word(b, 5) << 16));
        m.count = std::bit_cast<std::int16_t>(std::uint16_t(Word(b, 7)));
        if (!std::isfinite(m.cooldown) || m.cooldown < 0) return ParseResult::Malformed;
    }
    else if (Byte(b[1]) == 15)
    {
        if (b.size() != 3) return ParseResult::Malformed;
        m.kind = Kind::RetryCountdown;
        m.count = std::bit_cast<std::int8_t>(std::uint8_t(Byte(b[2])));
        if (m.count < -1 || m.count > 10) return ParseResult::Malformed;
    }
    else if (Byte(b[1]) == 16)
    {
        if (b.size() != 4 || Byte(b[2]) < 1 || Byte(b[2]) > 32 || Byte(b[3]) == 0)
            return ParseResult::Malformed;
        m.kind = Kind::RetryAnnouncement; m.index = Byte(b[2]); m.count = int(Byte(b[3]));
    }
    else if (Byte(b[1]) == 29)
    {
        // 02A5BCAA calls READ_LONG02AA4590 twice, never READ_SHORT.
        if (b.size() != 10) return ParseResult::Malformed;
        m.kind = Kind::RetryBudget;
        m.budget.remaining = std::bit_cast<std::int32_t>(Word(b, 2) | (Word(b, 4) << 16));
        m.budget.initial = std::bit_cast<std::int32_t>(Word(b, 6) | (Word(b, 8) << 16));
        if (!m.budget.Valid()) return ParseResult::Malformed;
    }
    else return ParseResult::Unrelated;
    out = m;
    return ParseResult::Accepted;
}

struct CenterMessage { unsigned token{}; int seconds{}; };
inline std::array<int, 3> CenterColor(const CenterMessage& message)
{
    // Original golden float RGB is (1, .843f, 0). Round to the nearest
    // channel value for the host's integer VGUI API; this is host quantization.
    return message.token == GoldenSpawnedToken || message.token == GoldenKilledToken
        ? std::array<int, 3>{255, 215, 0} : std::array<int, 3>{255, 180, 30};
}
inline ParseResult DecodeGoldenText(std::span<const std::byte> b, CenterMessage& out)
{
    if (b.empty() || Byte(b[0]) != 4) return ParseResult::Unrelated;
    // Recognize the fixed key before claiming ownership of malformed packets.
    // The stock adapter has one destination byte; CSO has exactly {4,0,7,0}.
    for (const std::size_t start : {std::size_t{1}, std::size_t{4}})
    {
        if (start >= b.size()) continue;
        // A stock literal can contain a Golden key after three characters.
        // Claim the extended layout only when its metadata identifies it;
        // printable prefixes must continue through the ordinary text chain.
        if (start == 4 && Byte(b[1]) != 0 && !(Byte(b[2]) == 7 && Byte(b[3]) == 0)) continue;
        const std::string_view tail{reinterpret_cast<const char*>(b.data() + start), b.size() - start};
        for (const unsigned token : {GoldenSpawnedToken, GoldenKilledToken})
        {
            const auto key = CenterTokens[token];
            if (!tail.starts_with(key) || (tail.size() > key.size() && tail[key.size()] != '\0')) continue;
            // Golden carries no parameters or duration override. Byte 7 is
            // the color selector, not a channel or a seven-second lifetime.
            if (tail.size() != key.size() + 1 || (start == 4 &&
                (Byte(b[1]) != 0 || Byte(b[2]) != 7 || Byte(b[3]) != 0)))
                return ParseResult::Malformed;
            out = {token, 0};
            return ParseResult::Accepted;
        }
    }
    return ParseResult::Unrelated;
}
inline ParseResult DecodeReloadWait(std::span<const std::byte> b, std::int32_t& out)
{
    if (b.empty() || Byte(b[0]) != 0x1c) return ParseResult::Unrelated;
    if (b.size() != 5) return ParseResult::Malformed;
    const auto seconds = std::bit_cast<std::int32_t>(Word(b, 1) | (Word(b, 3) << 16));
    if (seconds < 0) return ParseResult::Malformed;
    out = seconds;
    return ParseResult::Accepted;
}
inline ParseResult DecodeText(std::span<const std::byte> b, CenterMessage& out)
{
    const auto golden = DecodeGoldenText(b, out);
    if (golden != ParseResult::Unrelated) return golden;
    if (b.empty() || Byte(b[0]) != 4) return ParseResult::Unrelated;
    // CSO TextMsg has three metadata bytes after destination. The current
    // ReGameDLL TextMsg retains the native CS destination + strings format.
    std::size_t at = b.size() > 1 && Byte(b[1]) < 32 ? 4 : 1;
    if (at >= b.size()) return ParseResult::Malformed;
    const auto read = [&](std::string_view& value)
    {
        if (at >= b.size()) return false;
        const auto begin = at;
        while (at < b.size() && b[at] != std::byte{}) ++at;
        if (at == b.size() || at - begin > 127) return false;
        value = {reinterpret_cast<const char*>(b.data() + begin), at - begin};
        ++at;
        return true;
    };
    std::string_view token;
    if (!read(token)) return ParseResult::Malformed;
    // Golden is accepted only by its exact-layout decoder above. This legacy
    // metadata heuristic must not re-accept a packet that decoder declined.
    // Ambiguous control prefixes remain on the stock chain without changing
    // our center state; recognizable malformed Golden headers are rejected.
    const auto legacyEnd = CenterTokens.begin() + GoldenSpawnedToken;
    auto found = std::find(CenterTokens.begin(), legacyEnd, token);
    // The existing server sends this literal through stock TextMsg.
    const bool clearLiteral = token == "恭喜！！您已经成功通关！";
    if (found == legacyEnd && !clearLiteral) return ParseResult::Unrelated;
    CenterMessage m{clearLiteral ? 4u : unsigned(found - CenterTokens.begin()), 0};
    std::array<std::string_view, 4> arguments{};
    unsigned count{};
    while (at < b.size())
    {
        if (count == arguments.size() || !read(arguments[count++])) return ParseResult::Malformed;
    }
    if (m.token == 0 || m.token == 2)
    {
        if (!count || arguments[0].empty()) return ParseResult::Malformed;
        const auto result = std::from_chars(arguments[0].data(), arguments[0].data() + arguments[0].size(), m.seconds);
        if (result.ec != std::errc{} || result.ptr != arguments[0].data() + arguments[0].size() || m.seconds < 0)
            return ParseResult::Malformed;
    }
    out = m;
    return ParseResult::Accepted;
}

inline constexpr std::array<std::string_view, 2> HintTokens{
    "#CSO_ZBS_UseRevivalItem", "#CSO_ZBS_UseRetryItem"
};
inline ParseResult DecodeHint(std::span<const std::byte> b, bool withArguments, unsigned& out)
{
    std::size_t end{};
    while (end < b.size() && b[end] != std::byte{}) ++end;
    if (end == b.size()) return ParseResult::Malformed;
    const std::string_view token(reinterpret_cast<const char*>(b.data()), end);
    const auto found = std::find(HintTokens.begin(), HintTokens.end(), token);
    if (found == HintTokens.end()) return ParseResult::Unrelated;
    // 02A13720 / 02A135D0: string, isHint byte, optional argc byte.
    // Both original revival hints have no substitution arguments.
    if (b.size() != end + (withArguments ? 3 : 2)) return ParseResult::Malformed;
    if (Byte(b[end + 1]) == 0) return ParseResult::Unrelated;
    if (Byte(b[end + 1]) != 1 || (withArguments && Byte(b[end + 2]) != 0))
        return ParseResult::Malformed;
    out = unsigned(found - HintTokens.begin());
    return ParseResult::Accepted;
}

// Original HudText hint override: effect2, .01s/character, .07s color
// transition, y=.7, RGB(40,255,40)->RGB(0,255,0), fadeout=.7s.
// Its lifetime uses titles.txt's token length, while the typewriter uses
// localized character count. Keep those distinct (02A11E10 / 02A12570).
inline double HintHold(unsigned token)
{ return token < HintTokens.size() ? std::max(1, int((HintTokens[token].size() + 1) / 25)) : 0; }
inline double HintLifetime(unsigned token)
{ return token < HintTokens.size() ? (HintTokens[token].size() + 1) * .01 + HintHold(token) + .7 : 0; }
inline std::array<int, 3> HintColor(unsigned character, unsigned characters, unsigned token, double elapsed)
{
    if (!std::isfinite(elapsed) || elapsed < (character + 1) * .01) return {};
    const double letterAge = elapsed - (character + 1) * .01;
    const double fadeAt = characters * .01 + HintHold(token);
    int mix{};
    std::array<int, 3> target{};
    if (elapsed > fadeAt) mix = std::clamp(int((elapsed - fadeAt) / .7 * 255), 0, 255);
    else if (letterAge <= .07)
    {
        mix = std::clamp(int(255 - (letterAge / .07 * 255 + .5)), 0, 255);
        target = {0, 255, 0};
    }
    return {(mix * target[0] + 40 * (255 - mix)) >> 8,
        (mix * target[1] + 255 * (255 - mix)) >> 8,
        (mix * target[2] + 40 * (255 - mix)) >> 8};
}

struct Consumable { int count{-1}; double cooldownEnd{}; float cooldown{}; };
struct Rect { int x{}, y{}, width{}, height{}; };
struct RetryLayout { Rect banner, numbers; };
inline RetryLayout RetryRects(int width, int height)
{
    if (width <= 0 || height <= 0) return {};
    // Original 029C7ED3 uses screen height for both axes, with a fixed y120.
    // Keep its intermediate float values before truncating child bounds.
    const float panelHeight = float(height) * (230.f / 768.f);
    const float panelWidth = panelHeight * (500.f / 230.f);
    const float bannerHeight = panelHeight * (90.f / 230.f);
    const float numberSize = panelHeight - bannerHeight;
    const int x = int((float(width) - panelWidth) * .5f);
    return {{x, 120, int(panelWidth), int(bannerHeight)},
        {x + int((panelWidth - numberSize) * .5f), 120 + int(bannerHeight),
            int(numberSize), int(numberSize)}};
}
inline const char* RetrySound(int countdown)
{
    const char* path = countdown == -1 ? RetrySounds.back() :
        countdown >= 1 && countdown <= 10 ? RetrySounds[unsigned(countdown - 1)] : nullptr;
    return path ? path + 6 : nullptr; // Engine sound API omits the sound/ root.
}
inline std::wstring FormatRetryAnnouncement(std::wstring_view format, std::wstring_view player, unsigned round)
{
    std::wstring text;
    for (std::size_t i = 0; i < format.size(); ++i)
    {
        if (format[i] == L'%' && i + 1 < format.size())
        {
            if (format[i + 1] == L's') { text += player; ++i; continue; }
            if (format[i + 1] == L'd') { text += std::to_wstring(round); ++i; continue; }
        }
        text += format[i];
    }
    return text;
}
inline Rect OutcomeRect(int width, int height)
{
    if (width <= 0 || height <= 0) return {};
    // 029AD4F0 -> 029C2320 flags18, scale1; native image size410x73.
    return {int((512 - 410 / 2) * (float(width) / 1024)),
        int((165 - 73 / 2) * (float(height) / 768)),
        int(410 * (float(width) / 1024)), int(73 * (float(height) / 768))};
}
class State
{
public:
    void Reset() { *this = {}; }
    void ResetLife() { outcome_ = hint_ = retry_ = -1; center_ = {}; centerUntil_ = 0; }
    bool Apply(const Message& m, double now)
    {
        if (!std::isfinite(now) || now < 0) return false;
        if (m.kind == Kind::Round) { active_ = true; retry_ = -1; return true; }
        if (m.kind == Kind::Outcome)
        {
            if (m.index > 1) return false;
            active_ = true; outcome_ = int(m.index); outcomeAt_ = now;
            return true;
        }
        if (m.kind == Kind::RetryCountdown)
        {
            if (m.count < -1 || m.count > 10) return false;
            active_ = true; retry_ = m.count; return true;
        }
        if (m.kind == Kind::RetryAnnouncement)
        {
            if (m.index < 1 || m.index > 32 || m.count < 1 || m.count > 255) return false;
            active_ = true; return true;
        }
        if (m.kind == Kind::RetryBudget)
        {
            if (!m.budget.Valid()) return false;
            active_ = true; budget_ = m.budget; return true;
        }
        if (m.index >= items_.size() || !std::isfinite(m.cooldown) || m.cooldown < 0 ||
            m.count < -32768 || m.count > 32767 || !std::isfinite(now + m.cooldown)) return false;
        active_ = true;
        auto& item = items_[m.index];
        item.count = m.count;
        // A zero-cooldown count update does not erase a running cooldown in
        // the original consumer; only nonzero updates call 02A5D6D0.
        if (m.cooldown != 0) { item.cooldown = m.cooldown; item.cooldownEnd = now + m.cooldown; }
        return true;
    }
    bool SetMask(std::span<const std::byte> b)
    {
        if (b.size() != 2) return false;
        mask_ = Word(b, 0) & 15;
        return true;
    }
    bool SetCenter(const CenterMessage& message, double now, double duration)
    {
        if (message.token >= CenterTokens.size() || !std::isfinite(now) || now < 0 ||
            !std::isfinite(duration) || duration < 0 || !std::isfinite(now + duration)) return false;
        active_ = true; center_ = message; centerAt_ = now; centerUntil_ = now + duration;
        return true;
    }
    bool SetHint(unsigned token, double now)
    {
        if (token >= HintTokens.size() || !std::isfinite(now) || now < 0) return false;
        active_ = true; hint_ = int(token); hintAt_ = now;
        return true;
    }
    int Hint(double now) const
    { return hint_ >= 0 && std::isfinite(now) && now >= hintAt_ &&
        now < hintAt_ + HintLifetime(unsigned(hint_)) ? hint_ : -1; }
    double HintAge(double now) const { return now - hintAt_; }
    bool Active() const { return active_; }
    unsigned Mask() const { return mask_; }
    const auto& Items() const { return items_; }
    int Outcome() const { return outcome_; }
    int RetryCountdown() const { return retry_; }
    const RetryBudget& Budget() const { return budget_; }
    int OutcomeAlpha(double now) const
    {
        if (outcome_ < 0 || !std::isfinite(now)) return 0;
        const double elapsed = now - outcomeAt_;
        if (elapsed < 0 || elapsed >= 6.5) return 0;
        if (elapsed < .5) return std::clamp(int(elapsed / .5 * 255), 0, 255);
        if (elapsed < 5.5) return 255;
        return std::clamp(int((6.5 - elapsed) * 255), 0, 255);
    }
    const CenterMessage* Center(double now) const
    { return std::isfinite(now) && now >= centerAt_ && now < centerUntil_ ? &center_ : nullptr; }
    int CooldownRows(unsigned index, double now) const
    {
        if (index >= items_.size() || !std::isfinite(now)) return 27;
        const auto& item = items_[index];
        if (!item.cooldown) return 0;
        return int(27 * std::clamp((item.cooldownEnd - now) / item.cooldown, 0.0, 1.0));
    }
    // Called only after AMXX/engine menus have had first refusal. Keep R as
    // IN_RELOAD: MP chooses personal revival260 or round retry300 itself.
    const char* HealingCommand(int slot) const
    {
        if (!active_ || (slot != 5 && slot != 6)) return nullptr;
        const auto index = unsigned(slot - 5);
        // 02A5E350 checks inventory258/259, independently of HudIcon's
        // visibility mask. A late mask update must not disable an owned item.
        if (items_[index].count <= 0) return "";
        return slot == 5 ? "useitem \"zbs3\"\n" : "useitem \"zbs10\"\n";
    }
private:
    std::array<Consumable, 4> items_{};
    RetryBudget budget_;
    CenterMessage center_{};
    double centerAt_{}, centerUntil_{}, outcomeAt_{}, hintAt_{};
    unsigned mask_{};
    int outcome_{-1}, hint_{-1}, retry_{-1};
    bool active_{};
};
}

namespace csol::client
{
void InitializeScenarioRespawnUi();
void ResetScenarioRespawnUi();
void ScenarioRespawnUiFrame();
void ScenarioRespawnUiVideoInit();
void DrawScenarioRespawnUi(float time, int intermission);
void ShutdownScenarioRespawnUi();
bool TryScenarioHealingSlot(int oneBasedSlot);
void ScenarioRespawnUiDiagnostics(std::ostream&);
}
