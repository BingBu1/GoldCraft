#pragma once
#include "../include/goldcraft/vitals_protocol.hpp"

// These helpers use the game DLL's engine table, retaining Metamod/AMXX/ReAPI
// message interception. Unmodified clients keep their byte/short payloads.
inline bool GoldCraft_IntVitals(edict_t* receiver) {
    return receiver && !std::strcmp(GET_KEY_VALUE(GET_INFO_BUFFER(receiver), goldcraft::vitals::capability),
                                   goldcraft::vitals::version);
}
inline void GoldCraft_WriteHealth(edict_t* receiver, float value) {
    const auto health = goldcraft::vitals::integer(value, true);
    if (GoldCraft_IntVitals(receiver)) WRITE_LONG(health);
    else WRITE_BYTE((std::min)(health, 255));
}
inline void GoldCraft_WriteArmor(edict_t* receiver, float value) {
    const auto armor = goldcraft::vitals::integer(value, false);
    if (GoldCraft_IntVitals(receiver)) WRITE_LONG(armor);
    else WRITE_SHORT((std::min)(armor, 32767));
}
