#pragma once

// In-process ReHLDS extension. Include the host SDK's interface.h first.
// This is not an IPC structure and contains no addresses for hw.dll.
struct edict_s;
namespace goldcraft {
constexpr const char* host_entity_api_version="GoldCraftHostEntities001";
class IHostEntityPhysics : public IBaseInterface {
public:
    // Re-link accurate trigger hulls and dispatch actual solid contact callbacks.
    // The caller owns movement; this method never advances player simulation.
    virtual unsigned int TouchPlayerContacts(edict_s* player)=0;
};
}
