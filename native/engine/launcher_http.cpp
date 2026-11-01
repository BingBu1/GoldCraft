#include "launcher_http.h"
#include <algorithm>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace goldcraft::http {
namespace {
bool plain(const std::filesystem::path& path) {
    for (auto p = path; !p.empty();) {
        std::error_code ec;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(p, ec))) return false;
#ifdef _WIN32
        const auto attrs = GetFileAttributesW(p.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        const auto parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    return true;
}
std::string quote(const std::string& input, std::size_t limit) {
    std::string result = "\"";
    // Engine hostname may use a legacy codepage. Replace invalid UTF-8 bytes,
    // retaining complete valid sequences and never emitting invalid JSON.
    for (std::size_t i = 0; i < input.size() && i < limit;) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (c < 0x80) {
            if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
            else if (c < 32 || c == 127) result += ' ';
            else result += char(c);
            ++i; continue;
        }
        const std::size_t n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = n && i + n <= input.size() && i + n <= limit;
        for (std::size_t j = 1; valid && j < n; ++j)
            valid = (static_cast<unsigned char>(input[i+j]) & 0xc0) == 0x80;
        if (valid && n >= 3) {
            const auto d = static_cast<unsigned char>(input[i+1]);
            valid = !(c == 0xe0 && d < 0xa0) && !(c == 0xed && d >= 0xa0) &&
                    !(c == 0xf0 && d < 0x90) && !(c == 0xf4 && d >= 0x90);
        }
        if (valid) { result.append(input, i, n); i += n; }
        else { result += "\\ufffd"; ++i; }
    }
    return result + '"';
}
}
bool launcher_name(const std::string& name) {
    if (name == "launcher/v1/status.json" || name == "launcher/v1/manifest.json") return true;
    constexpr auto prefix = "launcher/v1/blobs/";
    constexpr std::size_t length = 18;
    static_assert(sizeof("launcher/v1/blobs/") - 1 == length);
    if (name.compare(0, length, prefix) || name.size() < length + 64 + 6 || name.size() > length + 64 + 82) return false;
    if (name.substr(length, 64).find_first_not_of("0123456789abcdef") != std::string::npos || name[length+64] != '/') return false;
    const auto file = name.substr(length + 65);
    if (file.size() < 5 || file.substr(file.size()-4) != ".dll") return false;
    const auto stem = file.substr(0, file.size()-4);
    return stem.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
}
bool LauncherFiles::configure(const std::filesystem::path& root) {
    std::error_code ec;
    const auto full = std::filesystem::absolute(root, ec).lexically_normal();
    if (root.empty() || ec || !plain(full) || full == full.root_path()) return false;
    std::lock_guard lock(mutex_); root_ = full; return true;
}
void LauncherFiles::publish_status(bool online, const std::string& name, const std::string& map,
                                  int players, int capacity, const std::string& address) {
    capacity = std::clamp(capacity, 0, 256); players = std::clamp(players, 0, capacity);
    const auto json = "{\"schema\":1,\"online\":" + std::string(online ? "true" : "false") +
        ",\"name\":" + quote(name,128) + ",\"map\":" + quote(map,64) +
        ",\"players\":" + std::to_string(players) + ",\"maxPlayers\":" + std::to_string(capacity) +
        ",\"gameAddress\":" + quote(address,128) + "}";
    auto bytes = std::make_shared<const Bytes>(json.begin(), json.end());
    std::lock_guard lock(mutex_); status_ = std::move(bytes);
}
Body LauncherFiles::request(const std::string& name) const {
    if (!launcher_name(name)) return {};
    std::filesystem::path root;
    { std::lock_guard lock(mutex_); if (root_.empty()) return {}; if (name == "launcher/v1/status.json") return status_; root = root_; }
    const auto path = root / name.substr(sizeof("launcher/v1/")-1);
    if (!plain(path)) return {};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return {};
    const auto size = std::filesystem::file_size(path, ec);
    const std::size_t limit = name == "launcher/v1/manifest.json" ? 1024*1024 : max_file_size;
    if (ec || !size || size > limit) return {};
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    auto bytes = std::make_shared<Bytes>(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(size)) || stream.peek() != std::char_traits<char>::eof()) return {};
    // Payload integrity/authenticity is checked against the pinned public key by
    // the launcher. Publication uses immutable hash paths and atomic manifest replacement.
    return bytes;
}
} // namespace goldcraft::http
