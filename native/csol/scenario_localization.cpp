// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_localization.hpp"

namespace csol::scenario_hud
{
namespace
{
class Tokens
{
public:
    explicit Tokens(std::u16string_view value) : text(value) {}
    bool Next(std::u16string& value)
    {
        value.clear();
        quotedToken = false;
        for (;;)
        {
            while (at < text.size() && (text[at] <= u' ' || text[at] == u'\ufeff')) ++at;
            if (at + 1 >= text.size() || text[at] != u'/' || text[at + 1] != u'/') break;
            while (at < text.size() && text[at] != u'\n') ++at;
        }
        if (at == text.size()) return false;
        if (text[at] == u'{' || text[at] == u'}') { value += text[at++]; return true; }
        const bool quoted = text[at] == u'"';
        quotedToken = quoted;
        if (quoted) ++at;
        while (at < text.size())
        {
            const auto c = text[at++];
            if (quoted && c == u'"') return true;
            if (!quoted && (c <= u' ' || c == u'{' || c == u'}')) { --at; return true; }
            if (quoted && c == u'\\' && at < text.size())
            {
                switch (text[at])
                {
                case u'n': value += u'\n'; ++at; continue;
                case u'r': value += u'\r'; ++at; continue;
                case u't': value += u'\t'; ++at; continue;
                case u'"': case u'\\': value += text[at++]; continue;
                default: break; // Preserve unknown escapes, including paths.
                }
            }
            value += c;
        }
        valid = !quoted;
        return valid;
    }
    bool Valid() const { return valid; }
    bool Quoted() const { return quotedToken; }
private:
    std::u16string_view text;
    std::size_t at{};
    bool valid{true};
    bool quotedToken{};
};
bool EqualAscii(std::u16string_view left, std::u16string_view right)
{
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const auto lower = [](char16_t c) { return c >= u'A' && c <= u'Z' ? c + (u'a' - u'A') : c; };
        if (lower(left[i]) != lower(right[i])) return false;
    }
    return true;
}
}
bool ReadLocalizedTokens(std::u16string_view text, const std::set<std::u16string>& wanted,
    LocalizedTokens& output, std::string& error)
{
    error.clear();
    const auto fail = [&](const char* reason) { error = reason; return false; };
    if (text.size() > 16 * 1024 * 1024 || text.find(u'\0') != text.npos)
        return fail("scenario localization size or embedded NUL is invalid");
    Tokens input(text);
    std::u16string key, value;
    while (input.Next(key))
    {
        if (!EqualAscii(key, u"Tokens")) continue;
        if (!input.Next(value) || input.Quoted() || value != u"{") return fail("localization Tokens block is invalid");
        LocalizedTokens result;
        while (input.Next(key))
        {
            if (!input.Quoted() && key == u"}") { output = std::move(result); return true; }
            // The shipped Chinese catalog has quoted [english] key prefixes,
            // but no conditional suffixes. Never apply an unsupported platform
            // condition as an unconditional replacement of a localized value.
            if (!input.Quoted() && key.starts_with(u'['))
                return fail("conditional localization entries are unsupported");
            if ((!input.Quoted() && key == u"{") || !input.Next(value) ||
                (!input.Quoted() && (value == u"{" || value == u"}")))
                return fail("localization token has no string value");
            if (wanted.contains(key)) result.insert_or_assign(key, std::move(value));
        }
        return fail(input.Valid() ? "localization Tokens block is unclosed" : "localization string is unclosed");
    }
    return fail("localization Tokens block is missing");
}
}
