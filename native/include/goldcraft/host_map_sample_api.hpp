#pragma once
#include <cstdint>

// Separate optional interface: GoldCraftMapPhysics001's vtable is unchanged.
// The engine owns native hull layouts and point-trace coordinate transforms.
namespace goldcraft {
constexpr const char *host_map_sample_api_version = "GoldCraftMapSample001";
struct HostMapCell { std::int32_t x, y, z; };
class IHostMapSample : public IBaseInterface {
  public:
    // Authenticated caller has already checked world occlusion and reach. This
    // retraces that target's current point hull and derives a model-local cell
    // from its original local plane, before native normal rotation/backoff.
    // Epoch/revision/slot/serial/model must still match. False leaves cell intact.
    virtual bool Sample(std::uint64_t epoch, std::uint64_t revision,
                        std::uint32_t slot, std::uint32_t serial, std::uint32_t model,
                        const float *start, const float *end, HostMapCell &cell) = 0;
};
} // namespace goldcraft
