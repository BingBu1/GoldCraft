// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "types.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <stop_token>

namespace csol::persistence {
struct PgOptions {
    // libpq service name resolves an operator-owned pg_service.conf. Secrets
    // belong in its private passfile; no password field, argv or cvar exists.
    std::string service="goldcraft";
    // Explicit numeric address avoids blocking DNS inside libpq startup.
    // Empty is disabled. Actual address comes only from private server config.
    std::string hostAddress;
    std::chrono::milliseconds connectTimeout{4000},queryTimeout{4000};
    EventId ledger;
};
class Store {
public:
    virtual ~Store()=default;
    virtual Reply Execute(const Request&,std::stop_token)=0;
};
std::unique_ptr<Store> MakePostgresStore(PgOptions);
}
