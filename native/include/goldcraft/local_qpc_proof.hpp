#pragma once
#include "goldcraft/wire.hpp"
#include <memory>

namespace goldcraft {
// A connection-local proof of access to this Windows logon session's mapping.
// It does not replace authentication, validate player state or synchronize clocks
// across machines. The caller must bind it to one authenticated socket epoch.
class LocalQpcProof {
public:
    static constexpr std::uint32_t magic = 0x31514347, layout = 1, mapping_bytes = 80;
    static std::unique_ptr<LocalQpcProof> create(std::uint64_t epoch);
    ~LocalQpcProof();
    LocalQpcProof(const LocalQpcProof&) = delete;
    LocalQpcProof& operator=(const LocalQpcProof&) = delete;
    Bytes offer() const;
    // Exactly one response. Malformed context/proof throws; a decline or elapsed
    // deadline returns false. Successful proof does not renew the time interval.
    bool acknowledge(std::span<const std::uint8_t> payload);
    std::uint64_t frequency() const noexcept { return frequency_; }
private:
    LocalQpcProof() = default;
    std::uint64_t epoch_ = 0, frequency_ = 0, issued_ = 0, deadline_ = 0;
    Key challenge_{};
    std::string name_;
    std::array<std::uint8_t, mapping_bytes> header_{};
    void* handle_ = nullptr;
    void* view_ = nullptr;
    bool answered_ = false;
};
}
