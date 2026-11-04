// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <cstddef>
#include <cstdint>

namespace csol::client
{
void ObserveStudioEventSequence(const cl_entity_t* entity, model_t* model, int flags);

inline const mstudioseqdesc_t* StudioSequence(const studiohdr_t* header, int sequence)
{
    if (!header || header->length < int(sizeof(studiohdr_t)) || sequence < 0 ||
        sequence >= header->numseq || header->seqindex < int(sizeof(studiohdr_t)) ||
        header->seqindex > header->length || std::size_t(header->numseq) >
        std::size_t(header->length - header->seqindex) / sizeof(mstudioseqdesc_t)) return nullptr;
    const auto* bytes = reinterpret_cast<const std::byte*>(header);
    return reinterpret_cast<const mstudioseqdesc_t*>(bytes + header->seqindex) + sequence;
}

// An entity's model may be the generic player model while StudioDrawPlayer
// uses SetupPlayerModel. Only the active studio header owns these event records.
inline bool StudioEventLooping(const mstudioevent_t* event, const cl_entity_t* entity,
    const cl_entity_t* activeEntity, const model_t* activeModel,
    const studiohdr_t* activeHeader, const void* modelHeader, bool& looping)
{
    if (!event || !entity || entity != activeEntity || !activeModel ||
        activeModel->type != mod_studio || !activeHeader || activeHeader != modelHeader ||
        activeHeader->length < int(sizeof(studiohdr_t))) return false;

    const auto length = std::size_t(activeHeader->length);
    const auto fits = [length](int offset, int count, std::size_t stride)
    {
        return offset >= int(sizeof(studiohdr_t)) && count > 0 &&
            std::size_t(offset) <= length && std::size_t(count) <= (length - std::size_t(offset)) / stride;
    };
    const auto* description = StudioSequence(activeHeader, entity->curstate.sequence);
    if (!description) return false;
    const auto* bytes = reinterpret_cast<const std::byte*>(activeHeader);
    if (!fits(description->eventindex, description->numevents, sizeof(mstudioevent_t))) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(bytes + description->eventindex);
    const auto address = reinterpret_cast<std::uintptr_t>(event);
    if (address < begin || (address - begin) % sizeof(mstudioevent_t) != 0 ||
        (address - begin) / sizeof(mstudioevent_t) >= std::size_t(description->numevents)) return false;
    looping = (description->flags & STUDIO_LOOPING) != 0;
    return true;
}
}
