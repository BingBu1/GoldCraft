// SPDX-License-Identifier: GPL-3.0-or-later
// Test preparation only: production parser/owner with controlled file edges.
#include "../../native/csol/scenario/damage_display_modes.hpp"
#include "../../native/csol/config_codec.hpp"
#include "../csol-scenario-difficulty/support.hpp"

#include <limits>

namespace
{
using namespace csol::scenario;
using namespace difficulty_test;
constexpr std::size_t MaximumSize = 8 * 1024 * 1024;

// Independent saved resource evidence, not a value produced by this parser.
// scenario/recovery-notification-20261028/options/mode-resource-verified.json
const std::vector<std::int32_t> OriginalModes{
    0,1,2,3,4,5,8,9,12,14,15,17,19,20,22,23,24,27,28,29,
    30,32,35,36,38,39,40,41,45,46,48,50,52,53,54,55,56,57,58,59};
const std::vector<std::int32_t> Sentinel{71, -82, 71};

std::string Document(std::string_view modes)
{ return "{\"FLOATING_DMG_STYLE\":{\"MODE\":" + std::string(modes) + "}}"; }

std::string CommentedDocument()
{
    // Eleven original CP949 comment bytes (not a translated UTF-8 copy).
    const std::string comment = "\xB1\xE2\xBA\xBB \xC0\xCC\xB9\xCC\xC1\xF6";
    return std::string("\xEF\xBB\xBF") + "// " + comment + "\r\n" +
        R"({"FLOATING_DMG_STYLE":{/* block comment */"MODE":[15,-3,15,2147483647,-2147483648],)" +
        R"("visual":"https://example.invalid/a/*literal*/ // \"quoted\"","unused":1,"unused":2}})";
}

void PlainCases()
{
    DamageDisplayModes modes{Sentinel}; ConfigError error{123, "previous error"};
    Require(ReadDamageDisplayModes(CommentedDocument(), modes, error), "BOM, CP949 comment bytes and both comment styles parse");
    Check(modes.modes == std::vector<std::int32_t>({15,-3,15,INT32_MAX,INT32_MIN}) &&
        modes.Contains(15) && modes.Contains(-3) && modes.Contains(INT32_MIN) && modes.Contains(INT32_MAX) && !modes.Contains(0),
        "signed32 limits, original order and duplicate elements survive without mode-range defaults");
    Check(error.byteOffset == 0 && error.message.empty(), "successful parse clears previous diagnostics");
    Check(ReadDamageDisplayModes(Document("[1,9]"), modes, error) && modes.modes == std::vector<std::int32_t>({1,9}) && !modes.Contains(15),
        "a resource that excludes scenario15 does not implicitly allow it");
    Check(ReadDamageDisplayModes(Document("[]"), modes, error) && modes.modes.empty() && !modes.Contains(15),
        "empty MODE is a valid empty permission list");
    auto exactLimit = Document("[15]"); exactLimit.resize(MaximumSize, ' ');
    Check(ReadDamageDisplayModes(exactLimit, modes, error) && modes.modes == std::vector<std::int32_t>({15}),
        "exact maximum plaintext size is accepted, including trailing whitespace");
}

void PlainRejections()
{
    std::vector<std::string> invalid{
        "", " ", "\xEF\xBB\xBF", "[]", "null", "{}", R"({"FLOATING_DMG_STYLE":[]})",
        R"({"FLOATING_DMG_STYLE":{}})", Document("null"), Document("{}"), Document("15"),
        Document("[15,true]"), Document("[15,null]"), Document("[15,\"1\"]"), Document("[15,1.0]"),
        Document("[15,1e0]"), Document("[15,{}]"), Document("[15,[]]"),
        Document("[15,2147483648]"), Document("[15,-2147483649]"), Document("[15,18446744073709551615]"),
        Document("[15,]"), Document("[+15]"), Document("[015]"), Document("[NaN]"),
        Document("[15]") + " trailing", Document("[15]") + "{}", "/* unterminated",
        R"({"FLOATING_DMG_STYLE":{"mode":[15]}})",
        R"({"FLOATING_DMG_STYLE":{"MODE":[15],"MODE":[9]}})",
        R"({"FLOATING_DMG_STYLE":{"MODE":[15]},"FLOATING_DMG_STYLE":{"MODE":[9]}})",
        R"({"FLOATING_DMG_STYLE":{"MODE":[15],"M\u004fDE":[]}})",
        R"({"FLOATING_DMG_STYLE":{"MODE\u0000extra":[15]}})"};
    auto embedded = Document("[15]"); embedded.insert(embedded.size() / 2, 1, '\0'); invalid.push_back(embedded);
    invalid.push_back(Document("[15]") + std::string(1, '\0'));
    invalid.emplace_back(MaximumSize + 1, ' ');
    for (const auto& text : invalid)
    {
        DamageDisplayModes modes{Sentinel}; ConfigError error;
        Check(!ReadDamageDisplayModes(text, modes, error) && modes.modes == Sentinel && !error.message.empty(),
            "invalid complete JSON, duplicate consumed keys or invalid MODE preserve caller output transactionally");
    }
}

void EncryptedCases()
{
    const auto encrypted = Encrypt(CommentedDocument()); const auto original = encrypted;
    DamageDisplayModes modes{Sentinel}; ConfigError error;
    Check(ReadDamageDisplayModesResource(Bytes(encrypted), modes, error) &&
        modes.modes == std::vector<std::int32_t>({15,-3,15,INT32_MAX,INT32_MIN}) && encrypted == original,
        "real decoder consumes complete blocks and NUL padding without mutating caller ciphertext");
    std::vector<std::string> invalid{"", std::string(7, 'x'), std::string(9, 'x'), std::string(MaximumSize + 8, 'x'),
        Encrypt("null"), Encrypt(Document("[15,\"bad\"]")), Encrypt(std::string(8, '\0'))};
    auto embedded = Document("[15]"); embedded.insert(1, 1, '\0'); invalid.push_back(Encrypt(embedded));
    for (const auto& bytes : invalid)
    {
        modes.modes = Sentinel; error = {};
        Check(!ReadDamageDisplayModesResource(Bytes(bytes), modes, error) && modes.modes == Sentinel && !error.message.empty(),
            "empty, partial, oversized or invalid decrypted resource cannot publish partial modes");
    }
}

void LoaderSnapshots()
{
    Files files; files.contents[DamageDisplayModesPath] = Encrypt(Document("[15,4,15]"));
    DamageDisplayModeResources resources; ResourceError error;
    Require(resources.Load(files.Api(), error), "actual resource owner loads synthetic encrypted fixture");
    const auto retained = resources.Snapshot();
    Require(retained && retained->modes == std::vector<std::int32_t>({15,4,15}), "immutable mode snapshot published");
    Check(files.order == std::vector<std::string>({std::string("load:") + DamageDisplayModesPath,
        std::string("free:") + DamageDisplayModesPath}) && files.buffers.empty(), "exact original path loads and releases once");
    files.contents[DamageDisplayModesPath] = Encrypt(Document("[]"));
    Require(resources.Load(files.Api(), error), "empty new map mode list loads");
    const auto replacement = resources.Snapshot();
    Check(replacement && replacement != retained && replacement->modes.empty() && !replacement->Contains(15) && retained->Contains(15),
        "new empty snapshot replaces owner state while in-flight old snapshot remains held");
    Check(resources.Clear() && !resources.Snapshot() && retained->modes == std::vector<std::int32_t>({15,4,15}) &&
        replacement->modes.empty() && resources.Clear(), "clear detaches current snapshot without mutating retained generations");
}

void LoaderFailures()
{
    for (int failure = 0; failure != 8; ++failure)
    {
        Files files; files.contents[DamageDisplayModesPath] = Encrypt(Document("[15]"));
        DamageDisplayModeResources resources; ResourceError error;
        Require(resources.Load(files.Api(), error), "seed previous resource snapshot");
        const auto retained = resources.Snapshot(); files.order.clear(); auto api = files.Api();
        if (failure == 0) files.contents.clear();
        if (failure == 1) files.reportedLength = 0;
        if (failure == 2) files.reportedLength = -1;
        if (failure == 3) files.reportedLength = 7;
        if (failure == 4) files.reportedLength = static_cast<int>(MaximumSize + 8);
        if (failure == 5) files.contents[DamageDisplayModesPath] = Encrypt(Document("[15,false]"));
        if (failure == 6) api.load = nullptr;
        if (failure == 7) api.release = nullptr;
        Check(!resources.Load(api, error) && !resources.Snapshot() && error.path == DamageDisplayModesPath &&
            !error.detail.message.empty() && retained && retained->Contains(15),
            "every failed outer load, including missing file API, detaches prior map without invalidating retained snapshot");
        Check(files.buffers.empty() && files.order.size() == (failure >= 6 ? 0 : failure == 0 ? 1 : 2),
            "non-null invalid buffers release once; missing buffer and unavailable API are not spuriously freed or loaded");
        files.contents[DamageDisplayModesPath] = Encrypt(Document("[9]")); files.reportedLength.reset();
        Check(resources.Load(files.Api(), error) && resources.Snapshot() && resources.Snapshot()->Contains(9) &&
            !resources.Snapshot()->Contains(15) && error.path.empty() && error.detail.message.empty(),
            "failed load unwinds its guard and next valid map can load without stale diagnostics");
    }
}

void LoaderReentrancy()
{
    for (bool malformed : {false, true})
    {
        Files files; files.contents[DamageDisplayModesPath] = Encrypt(Document("[15]"));
        DamageDisplayModeResources resources; ResourceError error;
        Require(resources.Load(files.Api(), error), "seed old owner before recursive callbacks");
        const auto retained = resources.Snapshot(); files.order.clear();
        files.contents[DamageDisplayModesPath] = Encrypt(Document(malformed ? "[9,\"bad\"]" : "[9]"));
        int loadCallbacks{}, releaseCallbacks{};
        const auto recursive = [&]
        {
            Check(!resources.Snapshot() && retained->Contains(15), "new load has detached old owner but not published candidate");
            // Deliberately share the caller's error object with the nested request.
            Check(!resources.Load(files.Api(), error) && !error.detail.message.empty() && !resources.Clear() &&
                !resources.Snapshot(), "recursive Load and Clear are rejected through both engine callback boundaries");
        };
        files.onLoad = [&](std::string_view path) { ++loadCallbacks; Check(path == DamageDisplayModesPath, "load callback uses original resource path"); recursive(); };
        files.onFree = [&](std::string_view) { ++releaseCallbacks; recursive(); };
        const bool loaded = resources.Load(files.Api(), error);
        Check(loaded == !malformed && loadCallbacks == 1 && releaseCallbacks == 1 && files.order.size() == 2 && files.buffers.empty(),
            "recursive requests do not invoke extra filesystem calls or break load/release pairing");
        if (malformed)
            Check(!resources.Snapshot() && error.path == DamageDisplayModesPath &&
                error.detail.message == "floating damage mode must be a signed 32-bit integer",
                "outer parse diagnostic survives release recursion that used the same error object");
        else
            Check(resources.Snapshot() && resources.Snapshot()->modes == std::vector<std::int32_t>({9}) &&
                error.path.empty() && error.detail.message.empty() && error.detail.byteOffset == 0,
                "outer success publishes only after release and removes nested busy diagnostic");
        files.onLoad = {}; files.onFree = {};
        Check(resources.Clear(), "loading guard releases after successful or failed callback sequence");
    }
}

void ActualResource(const std::filesystem::path& encryptedPath, const std::filesystem::path& plainPath)
{
    const auto encrypted = Read(encryptedPath), plain = Read(plainPath);
    Require(encrypted.size() == 135368 && plain.size() == 135364, "whole external resources match recorded evidence sizes");
    auto decoded = encrypted;
    Require(csol::DecodeConfiguration(std::as_writable_bytes(std::span(decoded.data(), decoded.size()))), "production decoder accepts actual external block length");
    while (!decoded.empty() && decoded.back() == '\0') decoded.pop_back();
    Check(decoded == plain, "whole decoded CP949 resource matches independent saved plaintext, not merely extracted MODE");
    DamageDisplayModes fromEncrypted{Sentinel}, fromPlain{Sentinel}; ConfigError error;
    Check(ReadDamageDisplayModesResource(Bytes(encrypted), fromEncrypted, error) && fromEncrypted.modes == OriginalModes && fromEncrypted.Contains(15),
        "actual encrypted resource matches all forty independently recorded modes in order");
    Check(ReadDamageDisplayModes(plain, fromPlain, error) && fromPlain.modes == OriginalModes && !fromPlain.Contains(6),
        "whole original commented plaintext parses with exact membership and no invented missing mode");
    Files files; files.contents[DamageDisplayModesPath] = encrypted; DamageDisplayModeResources resources; ResourceError resourceError;
    Check(resources.Load(files.Api(), resourceError) && resources.Snapshot() && resources.Snapshot()->modes == OriginalModes &&
        files.buffers.empty() && files.order.size() == 2, "actual bytes also pass the production snapshot and release path");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--synthetic"))
        { PlainCases(); PlainRejections(); EncryptedCases(); LoaderSnapshots(); LoaderFailures(); LoaderReentrancy(); }
        else if (argc == 4 && std::string_view(argv[1]) == "--actual") ActualResource(argv[2], argv[3]);
        else throw std::runtime_error("usage: --synthetic | --actual <whole encrypted jsoo> <whole saved decoded file>");
    }
    catch (const std::exception& error) { ++failures; std::cerr << "ERROR " << error.what() << '\n'; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
