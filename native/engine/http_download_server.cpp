#include "http_download_server.h"
#include "launcher_http.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
static void close_socket(Socket socket) { closesocket(socket); }
static void shutdown_socket(Socket socket) { shutdown(socket, SD_BOTH); }
static bool retry_socket() { const auto e = WSAGetLastError(); return e == WSAETIMEDOUT || e == WSAEWOULDBLOCK || e == WSAEINTR; }
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
using Socket = int;
constexpr Socket invalid_socket = -1;
static void close_socket(Socket socket) { close(socket); }
static void shutdown_socket(Socket socket) { shutdown(socket, SHUT_RDWR); }
static bool retry_socket() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
#endif

namespace goldcraft::http {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t workers = 8;
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
bool digits(const std::string& value, std::size_t& result) {
    if (value.empty() || value.size() > 10) return false;
    result = 0;
    for (const auto c : value) {
        if (c < '0' || c > '9' || result > 0xfffffffu) return false;
        result = result * 10 + (c - '0');
    }
    return true;
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string decode(const std::string& value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto c = value[i];
        if (c == '%') {
            if (i + 2 >= value.size() || hex(value[i + 1]) < 0 || hex(value[i + 2]) < 0) return {};
            c = static_cast<char>(hex(value[i + 1]) * 16 + hex(value[i + 2]));
            i += 2;
            if (c == '/' || c == '\\' || c == '%') return {};
        }
        result += c;
    }
    return result;
}
void socket_timeout(Socket socket) {
#ifdef _WIN32
    DWORD ms = 250;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval value{0, 250000};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value));
#endif
}
// A blocking Winsock send can time out after bytes have already entered the
// stream. Retrying the same buffer then duplicates data. All caller-owned
// sockets here are blocking; use explicit readiness only while sending, then
// restore the mode for the relay's bounded receives.
class NonblockingSend {
public:
    explicit NonblockingSend(Socket socket) : socket_(socket) {
#ifdef _WIN32
        u_long mode = 1;
        valid_ = ioctlsocket(socket_, FIONBIO, &mode) == 0;
#else
        flags_ = fcntl(socket_, F_GETFL, 0);
        valid_ = flags_ != -1 && fcntl(socket_, F_SETFL, flags_ | O_NONBLOCK) == 0;
#endif
    }
    ~NonblockingSend() {
        if (!valid_) return;
#ifdef _WIN32
        u_long mode = 0;
        ioctlsocket(socket_, FIONBIO, &mode);
#else
        fcntl(socket_, F_SETFL, flags_);
#endif
    }
    explicit operator bool() const { return valid_; }
private:
    Socket socket_;
    bool valid_ = false;
#ifndef _WIN32
    int flags_ = 0;
#endif
};
} // namespace

bool resource_name(const std::string& name) {
    if (name.empty() || name.size() > 255 || name.front() == '/') return false;
    for (unsigned char c : name)
        if (c < 32 || c > 126 || std::string("\\:%?<>\"|*~").find(static_cast<char>(c)) != std::string::npos) return false;
    std::size_t begin = 0;
    do {
        auto end = name.find('/', begin);
        if (end == std::string::npos) end = name.size();
        const auto part = lower(name.substr(begin, end - begin));
        if (part.empty() || part.front() == '.' || part.back() == '.' || part.back() == ' ') return false;
        const auto stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '0' && stem[3] <= '9')) return false;
        if (end == name.size()) break;
        begin = end + 1;
    } while (true);
    const auto slash = name.find('/'), dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    const auto ext = lower(name.substr(dot));
    if (slash == std::string::npos) {
        const auto n = lower(name);
        return ext == ".wad" && n != "cached.wad" && n != "tempdecal.wad" && n != "pldecal.wad" && n != "spraypaint.wad";
    }
    const auto root = lower(name.substr(0, slash));
    return (root == "maps" && (ext == ".bsp" || ext == ".res" || ext == ".txt")) ||
           lower(name) == "maps/zbs_message.cso" ||
           (root == "models" && (ext == ".mdl" || ext == ".seq" || ext == ".json" || ext == ".tga" || ext == ".bmp")) ||
           (root == "sound" && (ext == ".wav" || ext == ".mp3")) ||
           (root == "bgm" && ext == ".mp3") ||
           (root == "resource" && (ext == ".tga" ||
               (ext == ".ttf" && lower(name).compare(0, 14, "resource/font/") == 0))) ||
           lower(name) == "resource/csol/hud/v1/csolhudscheme.res" ||
           lower(name) == "resource/cso_chn.txt" ||
           (root == "sprites" && (ext == ".spr" || ext == ".txt" || ext == ".tga" || ext == ".webm")) ||
           (root == "gfx" && (ext == ".tga" || ext == ".bmp")) ||
           lower(name) == "gfx/tattoo/tattoo.cso" ||
           (root == "materials" && (ext == ".json" || ext == ".tga" || ext == ".bmp")) ||
           (root == "shader" && ext == ".txt") ||
           (root == "overviews" && (ext == ".txt" || ext == ".bmp" || ext == ".tga")) ||
           (root == "events" && ext == ".sc");
}

bool public_url(const std::string& url) {
    if (url.size() < 9 || url.size() > 127 || url.substr(0, 7) != "http://" || url.back() != '/') return false;
    for (unsigned char c : url)
        if (c <= 32 || c > 126 || std::string("\"'\\;@?#%").find(static_cast<char>(c)) != std::string::npos) return false;
    const auto slash = url.find('/', 7);
    if (slash == 7 || slash == std::string::npos || url.find("..", slash) != std::string::npos) return false;
    auto host = url.substr(7, slash - 7);
    const auto colon = host.find(':');
    if (colon != std::string::npos) {
        std::size_t port;
        if (!digits(host.substr(colon + 1), port) || port == 0 || port > 65535) return false;
        host.resize(colon);
    }
    if (host.empty()) return false;
    for (const auto c : host) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    // Routing is deliberately fixed, including when using an HTTP tunnel.
    return url.substr(slash) == "/goldcraft/";
}

struct Server::Impl {
    std::atomic<bool> stopping{true};
    Socket listener = invalid_socket;
    unsigned short bound_port = 0;
    Provider provider;
    std::thread acceptor;
    std::vector<std::thread> pool;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Socket> queue;
    std::unordered_set<Socket> sockets;
    unsigned short spray_port = 0;
    std::uint64_t spray_generation = 0;
    std::unordered_map<Socket, Socket> relays;
    std::atomic<std::uint64_t> requests{0}, bytes{0}, rejected{0};
#ifdef _WIN32
    bool winsock = false;
#endif

    bool send_all(Socket socket, const void* data, std::size_t size, Clock::time_point deadline) {
        if (!size) return true;
        const NonblockingSend mode(socket);
        if (!mode) return false;
        const auto* p = static_cast<const char*>(data);
        auto progress = Clock::now();
        while (size && !stopping && Clock::now() < deadline && Clock::now() - progress < std::chrono::seconds(5)) {
            const auto count = static_cast<int>(std::min(size, std::size_t(64 * 1024)));
#ifdef _WIN32
            const auto sent = send(socket, p, count, 0);
#else
            const auto sent = send(socket, p, count, MSG_NOSIGNAL);
#endif
            if (sent > 0) { p += sent; size -= sent; bytes += sent; progress = Clock::now(); }
            else {
                if (sent == 0) return false;
#ifdef _WIN32
                const auto error = WSAGetLastError();
                if (error == WSAEINTR) continue;
                if (error != WSAEWOULDBLOCK) return false;
#else
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
#endif
                fd_set writable;
                FD_ZERO(&writable); FD_SET(socket, &writable);
                timeval wait{0, 100000};
                const auto ready = select(static_cast<int>(socket) + 1, nullptr, &writable, nullptr, &wait);
                if (ready < 0) {
#ifdef _WIN32
                    if (WSAGetLastError() != WSAEINTR) return false;
#else
                    if (errno != EINTR) return false;
#endif
                }
            }
        }
        return size == 0;
    }
    bool reply(Socket socket, int status, std::size_t length = 0, const std::string& extra = {}) {
        const char* reason = status == 200 ? "OK" : status == 206 ? "Partial Content" :
            status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" :
            status == 416 ? "Range Not Satisfiable" : status == 503 ? "Service Unavailable" : "Bad Request";
        const auto header = "HTTP/1.1 " + std::to_string(status) + " " + reason +
            "\r\nServer: GoldCraftFastDL/2\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
            std::to_string(length) + "\r\nConnection: close\r\nCache-Control: no-store\r\nAccept-Ranges: bytes\r\n" + extra + "\r\n";
        if (status >= 400) ++rejected;
        return send_all(socket, header.data(), header.size(), Clock::now() + std::chrono::seconds(5));
    }
    void spray(Socket client, const std::string& header, std::size_t line, std::size_t end,
               const std::string& method, const std::string& target) {
        constexpr std::size_t limit = 10 * 1024 * 1024;
        const bool upload = method == "PUT";
        const std::string prefix = upload ? "/spray/upload/" : "/spray/image/";
        if ((!upload && method != "GET") || target.compare(0, prefix.size(), prefix) ||
            target.size() != prefix.size() + 32 || target.substr(prefix.size()).find_first_not_of("0123456789abcdef") != std::string::npos) {
            reply(client, 404); return;
        }
        std::size_t length = 0; bool have_length = false;
        for (auto pos = line + 2; pos < end;) {
            const auto next = header.find("\r\n", pos), colon = header.find(':', pos);
            if (next > end || colon >= next || colon == pos) { reply(client, 400); return; }
            const auto key = lower(header.substr(pos, colon - pos));
            auto value = header.substr(colon + 1, next - colon - 1);
            if (key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos ||
                std::any_of(value.begin(), value.end(), [](unsigned char c) { return (c < 32 && c != '\t') || c == 127; })) { reply(client, 400); return; }
            const auto a = value.find_first_not_of(" \t"), b = value.find_last_not_of(" \t");
            value = a == std::string::npos ? "" : value.substr(a, b - a + 1);
            if (key == "transfer-encoding" || key == "expect") { reply(client, 400); return; }
            if (key == "content-length") {
                if (have_length || !digits(value, length)) { reply(client, 400); return; }
                have_length = true;
            }
            pos = next + 2;
        }
        if ((upload && (!have_length || length < 22 || length > limit)) || (!upload && length) || header.size() - end - 4 > length) { reply(client, 400); return; }
        Socket backend = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (backend == invalid_socket) { reply(client, 503); return; }
        unsigned short port = 0; std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            // At least half of the fixed pool remains available for FastDL.
            if (!stopping && spray_port && relays.size() < workers / 2) {
                port = spray_port; generation = spray_generation; relays.emplace(client, backend);
            }
        }
        if (!port) { close_socket(backend); reply(client, 503); return; }
        struct Cleanup {
            Impl& self; Socket client, backend;
            ~Cleanup() { std::lock_guard<std::mutex> lock(self.mutex); self.relays.erase(client); close_socket(backend); }
        } cleanup{*this, client, backend};
        socket_timeout(backend);
        // Nonblocking connect keeps unavailable backends and shutdown bounded.
#ifdef _WIN32
        u_long nonblocking = 1; ioctlsocket(backend, FIONBIO, &nonblocking);
#else
        const auto flags = fcntl(backend, F_GETFL, 0); fcntl(backend, F_SETFL, flags | O_NONBLOCK);
#endif
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(backend, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        fd_set ready; FD_ZERO(&ready); FD_SET(backend, &ready); timeval wait{0, 250000};
        if (select(static_cast<int>(backend) + 1, nullptr, &ready, nullptr, &wait) <= 0) { reply(client, 503); return; }
        int error = 0;
#ifdef _WIN32
        int error_size = sizeof(error);
#else
        socklen_t error_size = sizeof(error);
#endif
        if (getsockopt(backend, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &error_size) || error) { reply(client, 503); return; }
        {
            std::lock_guard<std::mutex> lock(mutex);
            // A route can be retired while connect() is still pending, before
            // shutdown() has an established backend socket to interrupt.
            if (stopping || spray_generation != generation) return;
        }
#ifdef _WIN32
        nonblocking = 0; ioctlsocket(backend, FIONBIO, &nonblocking);
#else
        fcntl(backend, F_SETFL, flags);
#endif
        const auto deadline = Clock::now() + std::chrono::seconds(120);
        const auto canonical = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " + std::to_string(length) + "\r\nConnection: close\r\n\r\n";
        if (!send_all(backend, canonical.data(), canonical.size(), deadline) ||
            !send_all(backend, header.data() + end + 4, header.size() - end - 4, deadline)) return;
        std::array<char, 64 * 1024> buffer;
        auto receive = [&](Socket from, char* data, std::size_t count) {
            const auto idle = Clock::now() + std::chrono::seconds(5);
            while (!stopping && Clock::now() < deadline && Clock::now() < idle) {
                const int n = recv(from, data, static_cast<int>(count), 0);
                if (n > 0) return n;
                if (!n || !retry_socket()) return 0;
            }
            return 0;
        };
        for (auto remaining = length - (header.size() - end - 4); remaining;) {
            const auto n = receive(client, buffer.data(), std::min(remaining, buffer.size()));
            if (!n || !send_all(backend, buffer.data(), n, deadline)) return;
            remaining -= n;
        }
        std::string response;
        while (response.find("\r\n\r\n") == std::string::npos && response.size() < 8192) {
            const auto n = receive(backend, buffer.data(), 1024); if (!n) return;
            response.append(buffer.data(), n);
        }
        const auto head_end = response.find("\r\n\r\n"), first = response.find("\r\n");
        if (head_end == std::string::npos || head_end > 8192 || first >= head_end || response.substr(0, 9) != "HTTP/1.1 ") return;
        std::size_t code = 0, body_size = 0; bool body_length = false;
        if (!digits(response.substr(9, 3), code) || (code != 200 && code != 400 && code != 403 && code != 404 && code != 409 && code != 413 && code != 503)) return;
        for (auto pos = first + 2; pos < head_end;) {
            const auto next = response.find("\r\n", pos), colon = response.find(':', pos);
            if (colon >= next || next > head_end) return;
            const auto key = lower(response.substr(pos, colon - pos));
            auto value = response.substr(colon + 1, next - colon - 1);
            while (!value.empty() && value.front() == ' ') value.erase(0, 1);
            if (key == "transfer-encoding") return;
            if (key == "content-length") { if (body_length || !digits(value, body_size)) return; body_length = true; }
            pos = next + 2;
        }
        const auto initial = response.size() - head_end - 4;
        if (!body_length || body_size > limit || ((upload || code != 200) && body_size) || initial > body_size) return;
        if (!reply(client, static_cast<int>(code), body_size)) return;
        if (!send_all(client, response.data() + head_end + 4, initial, deadline)) return;
        for (auto remaining = body_size - initial; remaining;) {
            const auto n = receive(backend, buffer.data(), std::min(remaining, buffer.size()));
            if (!n || !send_all(client, buffer.data(), n, deadline)) return;
            remaining -= n;
        }
    }
    void serve(Socket socket) {
        std::string header;
        std::array<char, 1024> buffer;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (!stopping && Clock::now() < deadline && header.size() < 8192 && header.find("\r\n\r\n") == std::string::npos) {
            const auto n = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (n > 0) header.append(buffer.data(), n);
            else if (n == 0 || !retry_socket()) return;
        }
        const auto end = header.find("\r\n\r\n"), line = header.find("\r\n");
        if (end == std::string::npos || header.size() > 8192 || line == std::string::npos) { reply(socket, 400); return; }
        ++requests;
        const auto request = header.substr(0, line);
        const auto first = request.find(' '), last = request.rfind(' ');
        if (first == std::string::npos || first == last || (request.substr(last + 1) != "HTTP/1.0" && request.substr(last + 1) != "HTTP/1.1")) { reply(socket, 400); return; }
        const auto method = request.substr(0, first);
        const auto target = request.substr(first + 1, last - first - 1);
        if (target.compare(0, 7, "/spray/") == 0) { spray(socket, header, line, end, method, target); return; }
        if (method != "GET" && method != "HEAD") { reply(socket, 405); return; }
        // GoldSrc sends the literal # in CSOL texture filenames. Treat it as
        // part of the exact precached resource name, also accepting %23 from
        // conventional URL clients. Never strip a suffix as a fragment.
        if (target.substr(0, 11) != "/goldcraft/") { reply(socket, 404); return; }
        const auto name = decode(target.substr(11));
        if (!resource_name(name) && !launcher_name(name)) { reply(socket, 404); return; }
        std::string range;
        bool saw_range = false, saw_if_range = false;
        for (auto pos = line + 2; pos < end;) {
            auto next = header.find("\r\n", pos);
            const auto colon = header.find(':', pos);
            if (next == std::string::npos || colon == std::string::npos || colon >= next || header[pos] == ' ' || header[pos] == '\t') { reply(socket, 400); return; }
            const auto key = lower(header.substr(pos, colon - pos));
            auto value = header.substr(colon + 1, next - colon - 1);
            if (key.empty() || key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789!#$%&'*+-.^_`|~") != std::string::npos ||
                std::any_of(value.begin(), value.end(), [](unsigned char c) { return (c < 32 && c != '\t') || c == 127; })) {
                reply(socket, 400); return;
            }
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
            if (key == "range") {
                if (saw_range) { reply(socket, 400); return; }
                saw_range = true; range = value;
            }
            if (key == "if-range") saw_if_range = true;
            if (key == "transfer-encoding" || (key == "content-length" && value != "0")) { reply(socket, 400); return; }
            pos = next + 2;
        }
        auto body = provider(name);
        if (stopping) return;
        if (!body) { reply(socket, 404); return; }
        if (body->empty() || body->size() > max_file_size) { reply(socket, 503); return; }
        std::size_t begin = 0, finish = body->size() - 1;
        int status = 200;
        std::string extra;
        // No validator is advertised: an If-Range request must get the whole
        // current representation instead of mixing two map generations.
        if (saw_range && !saw_if_range) {
            const auto dash = range.find('-', 6);
            bool valid = range.substr(0, 6) == "bytes=" && dash != std::string::npos && range.find(',') == std::string::npos;
            std::size_t a = 0, b = 0;
            if (valid && dash == 6) {
                valid = digits(range.substr(dash + 1), b) && b > 0;
                begin = b >= body->size() ? 0 : body->size() - b;
            } else if (valid) {
                valid = digits(range.substr(6, dash - 6), a) && (dash + 1 == range.size() || digits(range.substr(dash + 1), b));
                begin = a;
                if (dash + 1 != range.size()) finish = std::min(finish, b);
            }
            if (!valid || begin >= body->size() || begin > finish) { reply(socket, 416, 0, "Content-Range: bytes */" + std::to_string(body->size()) + "\r\n"); return; }
            status = 206;
            extra = "Content-Range: bytes " + std::to_string(begin) + "-" + std::to_string(finish) + "/" + std::to_string(body->size()) + "\r\n";
        }
        if (!reply(socket, status, finish - begin + 1, extra)) return;
        if (method == "GET") send_all(socket, body->data() + begin, finish - begin + 1, Clock::now() + std::chrono::seconds(120));
    }
};

Server::Server() : impl_(new Impl) {}
Server::~Server() { stop(); }
bool Server::start(const std::string& bind, unsigned short port, Provider provider) {
    stop();
    auto& c = *impl_;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data)) return false;
    c.winsock = true;
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, bind.c_str(), &address.sin_addr) != 1) { stop(); return false; }
    c.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (c.listener == invalid_socket) { stop(); return false; }
#ifdef _WIN32
    BOOL exclusive = TRUE;
    setsockopt(c.listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif
    if (::bind(c.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(c.listener, 16)) { stop(); return false; }
#ifdef _WIN32
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    if (getsockname(c.listener, reinterpret_cast<sockaddr*>(&address), &size)) { stop(); return false; }
    c.bound_port = ntohs(address.sin_port);
    c.provider = std::move(provider);
    c.stopping = false;
    for (std::size_t i = 0; i < workers; ++i) c.pool.emplace_back([&c] {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        for (;;) {
            Socket socket;
            {
                std::unique_lock<std::mutex> lock(c.mutex);
                c.wake.wait(lock, [&] { return c.stopping || !c.queue.empty(); });
                if (c.stopping) return;
                socket = c.queue.front(); c.queue.pop_front();
            }
            c.serve(socket);
            std::lock_guard<std::mutex> lock(c.mutex);
            c.sockets.erase(socket);
            close_socket(socket);
        }
    });
    c.acceptor = std::thread([&c] {
        while (!c.stopping) {
            fd_set readable;
            FD_ZERO(&readable); FD_SET(c.listener, &readable);
            timeval timeout{0, 100000};
            if (select(static_cast<int>(c.listener) + 1, &readable, nullptr, nullptr, &timeout) <= 0) continue;
            const auto incoming = accept(c.listener, nullptr, nullptr);
            if (incoming == invalid_socket) continue;
            socket_timeout(incoming);
            bool accepted = false;
            {
                std::lock_guard<std::mutex> lock(c.mutex);
                if (!c.stopping && c.sockets.size() < workers) {
                    c.sockets.insert(incoming); c.queue.push_back(incoming); accepted = true;
                }
            }
            if (accepted) c.wake.notify_one();
            else {
                // A bounded, single send; overload cannot monopolise accept().
                const char response[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
#ifdef _WIN32
                send(incoming, response, sizeof(response) - 1, 0);
#else
                send(incoming, response, sizeof(response) - 1, MSG_NOSIGNAL);
#endif
                ++c.rejected; close_socket(incoming);
            }
        }
    });
    return true;
}

void Server::stop() {
    auto& c = *impl_;
    c.stopping = true;
    spray_backend(0);
    // Listener remains valid until acceptor has observed the stop flag.
    if (c.acceptor.joinable()) c.acceptor.join();
    if (c.listener != invalid_socket) { close_socket(c.listener); c.listener = invalid_socket; }
    {
        std::lock_guard<std::mutex> lock(c.mutex);
        for (auto socket : c.sockets) shutdown_socket(socket);
    }
    c.wake.notify_all();
    for (auto& worker : c.pool) worker.join();
    c.pool.clear();
    for (auto socket : c.queue) close_socket(socket);
    c.queue.clear(); c.sockets.clear(); c.provider = {};
    c.bound_port = 0;
#ifdef _WIN32
    if (c.winsock) { WSACleanup(); c.winsock = false; }
#endif
}
void Server::spray_backend(unsigned short port) {
    auto& c = *impl_; std::lock_guard<std::mutex> lock(c.mutex);
    if (c.spray_port == port) return;
    c.spray_port = port;
    ++c.spray_generation;
    for (const auto& relay : c.relays) { shutdown_socket(relay.first); shutdown_socket(relay.second); }
}
unsigned short Server::port() const { return impl_->bound_port; }
Stats Server::stats() const { return {impl_->requests.load(), impl_->bytes.load(), impl_->rejected.load()}; }
} // namespace goldcraft::http
