// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hd_spray.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace csol::spray::http {
using Token=std::array<std::uint8_t,16>;
struct Payload { Bytes bytes;std::shared_ptr<void> reservation; };
using Body=std::shared_ptr<const Payload>;
enum class State { Waiting, Receiving, Ready, Failed };
struct Upload {
    Token token;unsigned size,checksum;Dimensions original;
    std::atomic<State> state{State::Waiting};std::atomic<bool> cancelled{false};
    Body body; // Published before state becomes Ready (release/acquire).
    std::shared_ptr<void> reservation;
};
class Server {
public:
    Server();~Server();
    Server(const Server&)=delete;Server& operator=(const Server&)=delete;
    bool Start(const std::string& bind,unsigned short port);
    void Stop();unsigned short Port()const;
    std::shared_ptr<Upload> Offer(unsigned size,unsigned checksum,Dimensions original);
    Token Publish(Body body);
    size_t Reserved()const;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
