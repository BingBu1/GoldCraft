// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <cstring>

namespace csol::client {
// Decoded models can lack Renderer-owned render data (soundtable == 0).
// Its bone cache then aliases different skeletons, and a held model receives
// the player's array by index instead of StudioMergeBones' name mapping.
// Descriptor aliases also change the meaning of a sequence cache key. Keep
// this synchronous call on the native bone path; restore the user's setting.
class PlayerBoneCacheScope {
    cvar_t* variable{};
    float saved{};
public:
    explicit PlayerBoneCacheScope(cvar_t* cache):variable(cache) {
        if(variable){saved=variable->value;variable->value=0;}
    }
    ~PlayerBoneCacheScope(){if(variable)variable->value=saved;}
    PlayerBoneCacheScope(const PlayerBoneCacheScope&)=delete;
    PlayerBoneCacheScope& operator=(const PlayerBoneCacheScope&)=delete;
};
// Stock CS blends gait only for sequences < 101, except swim/treadwater.
// Expose extended upper-body tracks in two scratch descriptor slots during
// the synchronous studio call. Network IDs, model files and Renderer stay intact.
class PlayerSequenceScope {
    struct Descriptor {mstudioseqdesc_t* target{};mstudioseqdesc_t saved{};};
    struct Index {int* target{};int saved{};};
    std::array<Descriptor,2> descriptors{};
    std::array<Index,3> indices{};
    int descriptorCount{},indexCount{};
    bool mapped{};
    static bool UpperBody(const mstudioseqdesc_t& seq) {
        std::string_view name(seq.label,strnlen(seq.label,sizeof(seq.label)));
        if(name.starts_with("ref_"))name.remove_prefix(4);
        else if(name.starts_with("crouch_"))name.remove_prefix(7);
        else return false;
        return name.starts_with("aim_") || name.starts_with("shoot_") ||
            name.starts_with("shoot2_") || name.starts_with("reload_");
    }
    void IndexTo(int& value,int alias) {
        for(int i=0;i<indexCount;++i)if(indices[i].target==&value)return;
        indices[indexCount++]={&value,value};value=alias;
    }
public:
    PlayerSequenceScope(studiohdr_t* header,cl_entity_t* entity,entity_state_t* player) {
        if(!header || !entity || !player || header->numseq<=101 || header->numseq>2048 ||
            header->seqindex<0 || header->length<0 || size_t(header->seqindex)>size_t(header->length) ||
            size_t(header->numseq)>(size_t(header->length)-header->seqindex)/sizeof(mstudioseqdesc_t))return;
        auto* seq=reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<char*>(header)+header->seqindex);
        const int current=entity->curstate.sequence;
        if(current<101 || current>=header->numseq || !UpperBody(seq[current]) ||
            player->gaitsequence>=99 || entity->curstate.gaitsequence>=99)return;
        const int previous=entity->latched.prevsequence;
        // Never overwrite a native track still used for interpolation.
        const int alias=previous==100?99:100;
        descriptors[descriptorCount++]={seq+alias,seq[alias]};seq[alias]=seq[current];
        IndexTo(entity->curstate.sequence,alias);
        if(player->sequence==current)IndexTo(player->sequence,alias);
        if(previous==current)IndexTo(entity->latched.prevsequence,alias);
        else if(previous>=101 && previous<header->numseq && UpperBody(seq[previous])) {
            descriptors[descriptorCount++]={seq+99,seq[99]};seq[99]=seq[previous];
            IndexTo(entity->latched.prevsequence,99);
        }
        mapped=true;
    }
    ~PlayerSequenceScope() {
        for(int i=indexCount-1;i>=0;--i)*indices[i].target=indices[i].saved;
        for(int i=descriptorCount-1;i>=0;--i)*descriptors[i].target=descriptors[i].saved;
    }
    bool Mapped()const{return mapped;}
    PlayerSequenceScope(const PlayerSequenceScope&)=delete;
    PlayerSequenceScope& operator=(const PlayerSequenceScope&)=delete;
};
void PlayerAnimationDiagnostics(std::ostream& output);
}
