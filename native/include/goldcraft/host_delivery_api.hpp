#pragma once
#include <cstdint>

namespace goldcraft {
constexpr const char* host_delivery_api_version = "GoldCraftHostDelivery001";
class IHostDelivery : public IBaseInterface {
public:
    // Main engine thread only. Remaining optional user-message bytes after
    // reserving room for gameplay; includes type/length framing in the caller.
    virtual std::uint32_t ReliableRoom(std::uint32_t slot) const = 0;
};
}
