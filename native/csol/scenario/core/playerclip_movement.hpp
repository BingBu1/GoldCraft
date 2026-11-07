// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "playerclip.hpp"
#include <algorithm>
#include <cstring>

// Include the matching client/server pm_defs.h before this header. Engine
// model pointers stay in their own process and never enter the wire protocol.
namespace csol::playerclip
{
struct CollisionModel
{
    Descriptor clip;
    model_s* model{};
};

class Movement
{
public:
    bool Inject(playermove_t& move, std::span<const CollisionModel> clips)
    {
        if (move.numphysent < 1 || move.numphysent > MAX_PHYSENTS || clips.size() > Capacity) return false;
        // A client can predict several commands against the same engine-filled
        // list. Remove only our verified old tail at the NEXT entry, never on
        // return: SV_RunCmd still needs the returned ground and touch indices.
        if (m_move == &move && m_count && move.numphysent == m_base + int(m_count))
        {
            bool own = true;
            for (std::size_t i = 0; i < m_count; ++i)
                own = own && Same(move.physents[m_base+i], m_tail[i]);
            if (own) move.numphysent = m_base;
        }
        m_count = 0; m_move = &move; m_base = move.numphysent;
        std::array<const CollisionModel*, Capacity> append{};
        std::size_t count{};
        for (const auto& c : clips)
        {
            if (!c.model || !Valid(c.clip)) return false;
            bool exists = false;
            for (int i = 0; i < move.numphysent; ++i)
                if (move.physents[i].info == c.clip.entity) { exists = true; break; }
            if (!exists) append[count++] = &c;
        }
        if (count > std::size_t(MAX_PHYSENTS - move.numphysent)) return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& c = *append[i];
            auto& p = move.physents[move.numphysent++];
            p = {};
            std::memcpy(p.name, "playerclip", sizeof("playerclip"));
            p.info = c.clip.entity; p.model = c.model;
            // Original SOLID_NOT/skin0 uses the real BSP hull without making
            // an ordinary world entity or water/ladder content solid.
            p.solid = SOLID_NOT; p.skin = 0; p.movetype = MOVETYPE_NONE;
            // Client SDK uses vec3_t; ReGameDLL uses its Vector class.
            // Both expose component access, but Vector is not an iterator.
            for (unsigned axis = 0; axis != 3; ++axis)
            {
                p.origin[axis] = c.clip.origin[axis];
                p.angles[axis] = c.clip.angles[axis];
            }
            m_tail[m_count++] = p;
        }
        if (move.onground >= move.numphysent) move.onground = -1;
        return true;
    }
    void Reset() { m_move = nullptr; m_base = 0; m_count = 0; }
private:
    static bool Same(const physent_t& a, const physent_t& b)
    {
        for (unsigned axis = 0; axis != 3; ++axis)
            if (a.origin[axis] != b.origin[axis] || a.angles[axis] != b.angles[axis]) return false;
        return a.info == b.info && a.model == b.model && a.solid == b.solid && a.skin == b.skin &&
            a.player == b.player && a.movetype == b.movetype &&
            std::memcmp(a.name, b.name, sizeof(a.name)) == 0;
    }
    playermove_t* m_move{};
    int m_base{};
    std::size_t m_count{};
    std::array<physent_t, Capacity> m_tail{};
};
}
