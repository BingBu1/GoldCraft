#pragma once
#include "goldcraft/wire.hpp"
#include <memory>

namespace goldcraft {
constexpr std::uint32_t bulk_layout = 1;
constexpr std::uint32_t bulk_magic = 0x31424347;
constexpr std::uint32_t bulk_capacity = 64 * 1024 * 1024;
constexpr std::uint32_t bulk_header_bytes = 256;
constexpr std::uint32_t bulk_min_payload = 64 * 1024;
bool bulk_eligible(Type type);
bool bulk_internal(Type type);

// One consumer, one authenticated connection. Windows handles and pointers stay local.
class SharedBulk {
public:
    static std::unique_ptr<SharedBulk> create(std::uint64_t epoch);
    ~SharedBulk();
    SharedBulk(const SharedBulk&) = delete;
    SharedBulk& operator=(const SharedBulk&) = delete;
    Bytes offer() const;
    void acknowledge(std::span<const std::uint8_t> payload);
    Message consume(std::span<const std::uint8_t> reference);
    bool enabled() const { return enabled_; }
    bool acknowledged() const { return acknowledged_; }
    std::uint32_t high_water() const { return high_water_; }
private:
    SharedBulk() = default;
    void validate_header() const;
    void* handle_ = nullptr;
    std::uint8_t* view_ = nullptr;
    std::string name_;
    std::uint64_t epoch_ = 0;
    std::uint32_t read_cursor_ = 0, high_water_ = 0;
    bool enabled_ = false, acknowledged_ = false;
    friend struct SharedBulkTestAccess;
};
}
