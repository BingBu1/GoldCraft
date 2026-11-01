// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../hd_spray_http.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace csol::client::spray_http {
struct Reservation {
    std::shared_ptr<std::atomic<size_t>> counter;size_t size{};
    ~Reservation(){if(counter)*counter-=size;}
};
inline std::shared_ptr<Reservation> Reserve(const std::shared_ptr<std::atomic<size_t>>& counter,size_t size){
    auto lease=std::make_shared<Reservation>();lease->counter=counter;auto used=counter->load();
    do{spray::Check(used<=spray::CacheBytes&&size<=spray::CacheBytes-used);}while(!counter->compare_exchange_weak(used,used+size));
    lease->size=size;return lease;
}
inline std::shared_ptr<const spray::Animation> Decode(std::span<const std::uint8_t> bytes,std::shared_ptr<Reservation> lease){
    struct Owned {std::shared_ptr<Reservation> lease;spray::Animation animation;};
    if(lease)spray::Check(spray::InspectAnimation(bytes).memory<=lease->size);
    auto owned=std::make_shared<Owned>();owned->lease=std::move(lease);owned->animation=spray::DecodeAnimation(bytes);
    if(owned->lease){auto& budget=*owned->lease;const auto memory=owned->animation.Memory();*budget.counter-=budget.size-memory;budget.size=memory;}
    return {owned,&owned->animation};
}
struct Job {
    unsigned epoch{},id{},size{},checksum{};std::string host,path;unsigned short port{};
    bool upload{},secure{};size_t decodedLimit{size_t(spray::MaxPixels)*4};spray::Bytes bytes;
    std::shared_ptr<const spray::Animation> animation;
    std::shared_ptr<Reservation> reservation;
    std::atomic<unsigned> transferred{};
    std::atomic<bool> cancelled{},done{};
    bool success{}; // Only read after done(acquire).
};
class Worker {
public:
    Worker();~Worker();
    Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    bool Submit(std::shared_ptr<Job> job);
    void Cancel();void Shutdown();
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
