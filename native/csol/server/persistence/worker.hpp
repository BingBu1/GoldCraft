// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "journal.hpp"
#include "store.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace csol::persistence {
enum class Submission {Queued,Busy,Invalid,NotReady,Stopped};
enum class WorkerState {Starting,Ready,StorageFailed,Stopped};
class Worker final {
public:
    Worker(std::filesystem::path outbox,EventId ledger,std::unique_ptr<Store>);
    ~Worker();
    Worker(const Worker&)=delete;
    Worker& operator=(const Worker&)=delete;
    Submission Submit(Request);
    std::optional<Notice> Poll();
    // After a confirmed debit the gameplay owner must first claim and apply
    // exactly once, or commit a RefundDebit referencing that debit. Ack only
    // after one of those dispositions. Pending debit records survive restart.
    bool Acknowledge(const EventId&);
    WorkerState State()const{return state_.load();}
    void RequestStop();
    // Destruction joins: only process shutdown may destroy the worker, never
    // a frame, map reload, disconnect, character change or respawn callback.
private:
    struct Work {Request request;Reply reply;bool recovered{},finished{},failedNotice{};};
    void Run(std::stop_token);
    bool Publish(Notice,std::stop_token);
    static constexpr std::size_t QueueLimit=256,PendingLimit=10000;
    EventId ledger_;
    Journal journal_;
    std::unique_ptr<Store> store_;
    std::atomic<WorkerState> state_{WorkerState::Starting};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Request> requests_;
    std::deque<EventId> acknowledgements_;
    std::deque<Notice> notices_;
    std::unordered_map<EventId,Work,EventHash> pending_;
    std::jthread thread_;
};
}
