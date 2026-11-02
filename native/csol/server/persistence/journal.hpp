// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.hpp"
#include <filesystem>
#include <vector>

namespace csol::persistence {
// Only the persistence worker may use this class. Files are independent,
// private runtime data; never include this directory in FastDL or publication.
class Journal final {
public:
    explicit Journal(std::filesystem::path directory,EventId ledger);
    ~Journal();
    Journal(const Journal&)=delete;
    Journal& operator=(const Journal&)=delete;
    bool Open();
    bool Load(std::vector<Request>& output);
    bool Put(const Request&);
    bool Erase(const EventId&);
private:
    std::filesystem::path directory_;
    EventId ledger_;
    void* lock_=reinterpret_cast<void*>(-1);
};
}
