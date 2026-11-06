// SPDX-License-Identifier: GPL-3.0-or-later
// Real loopback transport plus the deployed Store. The input is the prepared,
// hash-verified wall scheme; the build runner selects exact frozen host headers.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "http_download_store.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <thread>

using namespace goldcraft::http;
using namespace std::chrono_literals;
namespace {
unsigned checks{}, failures{};
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Response { int status{}; std::string headers; Bytes body; };
Response Request(unsigned short port, const std::string& path,
                 const std::string& method = "GET", const std::string& extra = {}) {
    const auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Require(socket != INVALID_SOCKET, "socket");
    struct Close { SOCKET socket; ~Close() { closesocket(socket); } } close{socket};
    DWORD timeout = 5000;
    Require(setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout)) == 0, "receive timeout");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    Require(connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "connect");
    const auto query = method + " /goldcraft/" + path + " HTTP/1.1\r\nHost: localhost\r\n" + extra + "\r\n";
    std::size_t sent{};
    while (sent < query.size()) {
        const auto count = send(socket, query.data() + sent, int(query.size() - sent), 0);
        Require(count > 0, "send");
        sent += count;
    }
    std::string wire;
    char buffer[4096];
    for (;;) {
        const auto count = recv(socket, buffer, sizeof(buffer), 0);
        Require(count >= 0, "receive");
        if (!count) break;
        wire.append(buffer, count);
    }
    const auto end = wire.find("\r\n\r\n");
    Require(end != std::string::npos && wire.starts_with("HTTP/1.1 "), "HTTP response");
    return {std::stoi(wire.substr(9, 3)), wire.substr(0, end),
            Bytes(wire.begin() + end + 4, wire.end())};
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "usage: http-wall-scheme-test prepared-scheme.res");
        std::ifstream input(argv[1], std::ios::binary);
        Require(bool(input), "open scheme");
        const Bytes scheme{std::istreambuf_iterator<char>(input), {}};
        Require(scheme.size() == 222, "exact prepared scheme length");
        const std::string wall = "resource/csol/wall/v1/WallUpgradeScheme.res";
        const std::string upper = "RESOURCE/CSOL/WALL/V1/WALLUPGRADESCHEME.RES";
        const std::string oldHud = "resource/csol/hud/v1/CSOLHudScheme.res";
        const std::vector<std::string> denied{
            "resource/csol/wall/v1/Other.res",
            "resource/csol/wall/v1/WallUpgradeScheme.res.bak",
            "resource/csol/wall/v1/WallUpgradeScheme2.res",
            "resource/csol/wall/v2/WallUpgradeScheme.res",
            "resource/csol/wall/WallUpgradeScheme.res",
            "resource/csol/hud/v1/WallUpgradeScheme.res",
            "resource/ClientScheme.res",
            "resource/csol/wall/v1/../WallUpgradeScheme.res",
            "resource/csol/wall/v1//WallUpgradeScheme.res",
            "resource/csol/wall/v1/WallUpgradeScheme.res ",
            "resource/csol/wall/v1/WallUpgradeScheme.res:stream",
            "resource/csol/wall/v1/WallUpgradeScheme.res?x=1",
            "resource/csol/wall/v1/WallUpgradeScheme.res#x",
            "resource%2fcsol/wall/v1/WallUpgradeScheme.res",
            "resource/csol/wall/v1/%2e%2e/WallUpgradeScheme.res",
            "resource/csol/wall/v1/WallUpgradeScheme.res%00",
        };
        Check(resource_name(wall), "canonical wall path allowed");
        Check(resource_name(upper), "case-insensitive wall policy");
        Check(resource_name(oldHud), "existing HUD scheme retained");
        for (const auto& name : denied) Check(!resource_name(name), "similar or unsafe path denied");
        std::map<std::string, Bytes> files{{wall, scheme}, {upper, scheme}, {oldHud, Bytes{'o','l','d','-','h','u','d'}}};
        Store store;
        std::atomic<unsigned> providerCalls{};
        Server server;
        Require(server.start("127.0.0.1", 0, [&](const std::string& name) {
            ++providerCalls; return store.request(name);
        }), "start isolated HTTP");
        const auto port = server.port();
        auto request = [&](const std::string& path, const std::string& method = "GET", const std::string& extra = {}) {
            auto future = std::async(std::launch::async, [&] { return Request(port, path, method, extra); });
            const auto deadline = std::chrono::steady_clock::now() + 8s;
            while (future.wait_for(0ms) != std::future_status::ready) {
                if (auto load = store.take()) {
                    const auto found = files.find(load->name);
                    Require(found != files.end(), "only published fixture file loaded");
                    *load->bytes = found->second;
                    store.finish(load, true);
                }
                Require(std::chrono::steady_clock::now() < deadline, "store pump timeout");
                std::this_thread::sleep_for(1ms);
            }
            return future.get();
        };
        Check(request(wall).status == 404, "not advertised remains 404");
        std::vector<Resource> resources{{wall, scheme.size()}, {upper, scheme.size()}, {oldHud, files.at(oldHud).size()}};
        for (const auto& name : denied) resources.push_back({name, scheme.size()});
        store.publish(resources);
        Check(store.stats().files == 3, "Store retains only three exact advertised paths");
        const auto full = request(wall);
        std::cout << "wall status=" << full.status << " bytes=" << full.body.size() << '\n';
        Check(full.status == 200 && full.body == scheme, "GET transports exact prepared scheme bytes");
        const auto head = request(wall, "HEAD");
        Check(head.status == 200 && head.body.empty() &&
              head.headers.find("Content-Length: 222") != std::string::npos, "HEAD preserves scheme length without body");
        const auto range = request(wall, "GET", "Range: bytes=7-25\r\n");
        Check(range.status == 206 && range.body == Bytes(scheme.begin() + 7, scheme.begin() + 26) &&
              range.headers.find("Content-Range: bytes 7-25/222") != std::string::npos, "range transports exact subset");
        Check(request("resource/csol/wall/v1/%57allUpgradeScheme.res").body == scheme, "safe percent decoding retains exact path");
        Check(request(upper).body == scheme, "advertised uppercase path accepted");
        const auto existing = request(oldHud);
        Check(existing.status == 200 && existing.body == files.at(oldHud), "existing HUD path still works");
        for (const auto& name : denied) {
            const auto calls = providerCalls.load();
            Check(request(name).status == 404, "HTTP rejects lookalike path");
            Check(providerCalls.load() == calls, "rejected path never reaches provider");
            Check(!store.request(name), "Store rejects unsafe advertised entry");
        }
        store.publish({});
        Check(request(wall).status == 404, "retired advertisement returns 404");
        server.stop();
        std::cout << "checks=" << checks << " failures=" << failures << '\n';
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "EXCEPTION " << error.what() << '\n';
        return 2;
    }
}
