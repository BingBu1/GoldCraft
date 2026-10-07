#pragma once

// Included after ReHLDS precompiled.h. server_t remains byte-for-byte compatible
// with the upstream ABI; its original low slots are mirrored by these setters.
#include <array>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "goldcraft/precache_protocol.hpp"

struct GoldCraftPrecache {
    std::vector<const char*> modelNames;
    std::vector<model_t*> models;
    std::vector<unsigned char> modelFlags;
    std::vector<const char*> sounds;
    std::vector<std::string> generics;
    std::deque<resource_t> resources;
    std::unordered_map<std::string, int> modelIndices;
    std::unordered_map<std::string, int> soundIndices;
    std::unordered_map<std::string, int> genericIndices;
    std::deque<std::array<char, 16>> inlineNames;

    void Reset();
    int ModelIndex(const char* name) const;
    int SoundIndex(const char* name) const;
    int AddModel(const char* name, model_t* model, unsigned char flags);
    int AddSound(const char* name);
    int AddGeneric(const char* name);
    const char* InlineName(int index);
    bool RequiresExtension() const;
};

extern GoldCraftPrecache gc_precache;
bool GC_PrecacheClient(const client_t* client);
bool GC_CheckPrecacheClient(client_t* client);
void GC_SendResourceManifest(client_t* client);
bool GC_WriteResourceChunk(sizebuf_t* message);
void GC_PrecacheInit();
void GC_WidenModelDelta(delta_t* delta);
void GC_WriteMediaIndex(sizebuf_t* message, int index);
void GC_WriteSoundIndex(int index, int bits);
void GC_MessageBegin();
bool GC_MessageMediaField();
bool GC_WriteMessageMedia(int index);

model_t* GC_CachedModel(int index);
model_t* GC_NewCachedModel(qboolean trackCRC);
mod_known_info_t* GC_ModelCRC(model_t* model);
mod_known_info_t* GC_ModelCRC(int index);
