#pragma once
#include <cstdint>

// Private in-process engine/GameDLL interface; use the matching interface.h.
// Payloads use values, never engine layout guesses or shared ownership.
namespace goldcraft {
constexpr const char *host_map_api_version = "GoldCraftMapPhysics001";
struct HostMapCut {
    std::uint32_t slot, serial, model;
    float min[3], max[3];
};
struct HostMapStats {
    std::uint64_t epoch, revision, traces, points, failures;
    std::uint32_t targets;
};
class IHostMapPhysics : public IBaseInterface {
  public:
    // Compile every affected model/hull before atomically replacing live state.
    // False leaves the previous state intact. No rendering success is implied.
    virtual bool Replace(std::uint64_t epoch, std::uint64_t revision, const HostMapCut *cuts,
                         std::uint32_t count) = 0;
    virtual void Reset(std::uint64_t epoch, std::uint64_t revision) = 0;
    virtual const char *LastError() const = 0;
    virtual HostMapStats Stats() const = 0;
};
} // namespace goldcraft
