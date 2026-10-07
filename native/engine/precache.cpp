#include "precompiled.h"
#include "precache.h"
#include <algorithm>
#include <cctype>
#include <limits>

GoldCraftPrecache gc_precache;

namespace {
std::string SoundKey(const char* name) {
    std::string key(name ? name : "");
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

struct CachedModel {
    model_t model{};
    mod_known_info_t crc{};
};
std::vector<std::unique_ptr<CachedModel>> cachedModels;
std::unordered_map<model_t*, CachedModel*> modelRecords;
client_t* manifestClient = nullptr;
unsigned manifestFirst = 0;
unsigned messageExtraBytes = 0;
cvar_t precacheVersion = {"gc_precache_protocol", "2", FCVAR_SERVER};

void PrecacheStats() {
    Con_Printf("GoldCraft precache v2 (32-bit media): models=%u sounds=%u generic=%u resources=%u cached=%u extended=%d\n",
        unsigned(gc_precache.modelNames.size()), unsigned(gc_precache.sounds.size()),
        unsigned(gc_precache.generics.size()), unsigned(gc_precache.resources.size()),
        unsigned(cachedModels.size()), gc_precache.RequiresExtension());
}

void PrecacheFixturePad() {
    // Explicitly opt-in test fixture. Reserve holes so actual handles65535 and
    //65536 can be exercised without generating65000 duplicate asset files.
    if (!COM_CheckParm("-goldcraft_precache_test") || g_psv.state != ss_loading ||
        gc_precache.models.size() >= 65535 || gc_precache.sounds.size() >= 65535) {
        Con_Printf("GoldCraft precache fixture padding is unavailable\n");
        return;
    }
    gc_precache.models.resize(65535);
    gc_precache.modelNames.resize(65535);
    gc_precache.modelFlags.resize(65535);
    gc_precache.sounds.resize(65535);
    Con_Printf("GoldCraft precache fixture reserved sparse slots through65534\n");
}
}

void GoldCraftPrecache::Reset() {
    manifestClient = nullptr;
    manifestFirst = 0;
    modelNames.assign(1, pr_strings);
    models.assign(1, nullptr);
    modelFlags.assign(1, 0);
    sounds.assign(1, pr_strings);
    generics.clear();
    resources.clear();
    modelIndices.clear();
    soundIndices.clear();
    genericIndices.clear();
    inlineNames.clear();
}

int GoldCraftPrecache::ModelIndex(const char* name) const {
    if (!name || !*name) return 0;
    const auto it = modelIndices.find(name);
    return it == modelIndices.end() ? 0 : it->second;
}

int GoldCraftPrecache::SoundIndex(const char* name) const {
    const auto it = soundIndices.find(SoundKey(name));
    return it == soundIndices.end() ? 0 : it->second;
}

int GoldCraftPrecache::AddModel(const char* name, model_t* model, unsigned char flags) {
    if (modelNames.size() > goldcraft::precache::max_media_index)
        Host_Error("GoldCraft: model '%s' exceeds the signed 32-bit engine API range", name);
    const int index = static_cast<int>(modelNames.size());
    const char* saved = ED_NewString(name);
    modelNames.push_back(saved);
    models.push_back(model);
    modelFlags.push_back(flags);
    modelIndices.emplace(saved, index);
    if (index < MAX_MODELS) {
        g_psv.model_precache[index] = saved;
        g_psv.models[index] = model;
        g_psv.model_precache_flags[index] = flags;
    }
    return index;
}

int GoldCraftPrecache::AddSound(const char* name) {
    if (sounds.size() > goldcraft::precache::max_media_index)
        Host_Error("GoldCraft: sound '%s' exceeds the signed 32-bit engine API range", name);
    const int index = static_cast<int>(sounds.size());
    const char* saved = ED_NewString(name);
    sounds.push_back(saved);
    soundIndices.emplace(SoundKey(name), index);
    if (index < MAX_SOUNDS) g_psv.sound_precache[index] = saved;
    return index;
}

int GoldCraftPrecache::AddGeneric(const char* name) {
    const auto key = SoundKey(name);
    const auto found = genericIndices.find(key);
    if (found != genericIndices.end()) return found->second;
    if (g_psv.state != ss_loading)
        Host_Error("%s: '%s' Precache can only be done in spawn functions", __func__, name);
    if (generics.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
        Host_Error("GoldCraft: generic resource index exceeds the signed 32-bit API range");
    const int index = static_cast<int>(generics.size());
    generics.emplace_back(name);
    genericIndices.emplace(key, index);
    return index;
}

const char* GoldCraftPrecache::InlineName(int index) {
    if (index < 0 || !g_psv.worldmodel || index >= g_psv.worldmodel->numsubmodels)
        Host_Error("GoldCraft: invalid inline model %d", index);
    while (inlineNames.size() <= static_cast<size_t>(index)) {
        std::array<char, 16> name{};
        Q_snprintf(name.data(), name.size(), "*%u", unsigned(inlineNames.size()));
        inlineNames.push_back(name);
    }
    return inlineNames[index].data();
}

bool GoldCraftPrecache::RequiresExtension() const {
    if (modelNames.size() > MAX_MODELS || sounds.size() > MAX_SOUNDS ||
        resources.size() >= goldcraft::precache::marker) return true;
    size_t bytes = 512;
    for (size_t i = 0; i < resources.size(); ++i) {
        const auto& r = resources[i];
        if (r.nIndex >= int(goldcraft::precache::marker) ||
            ((r.ucFlags & RES_CHECKFILE) && i > 1023)) return true;
        bytes += std::strlen(r.szFileName) + 60;
    }
    return bytes >= NET_MAX_PAYLOAD;
}

bool GC_PrecacheClient(const client_t* client) {
    return client && !Q_strcmp(Info_ValueForKey(client->userinfo, goldcraft::precache::capability), goldcraft::precache::capability_value);
}

bool GC_CheckPrecacheClient(client_t* client) {
    if (!gc_precache.RequiresExtension() || GC_PrecacheClient(client) || client->fakeclient) return true;
    SV_DropClient(client, FALSE, "This map requires GoldCraft precache v2 (32-bit MetaHook extension).");
    return false;
}

void GC_SendResourceManifest(client_t* client) {
    const auto total = static_cast<unsigned>(gc_precache.resources.size());
    unsigned first = 0;
    manifestClient = client;
    do {
        // A bounded group fits the existing reliable fragment and decompression
        // limits. Multiple groups are queued in order; no enlarged UDP packet.
        byte data[NET_MAX_PAYLOAD];
        sizebuf_t msg{};
        msg.buffername = "GoldCraftResources";
        msg.data = data;
        msg.maxsize = sizeof(data);
        msg.flags = SIZEBUF_CHECK_OVERFLOW;
        manifestFirst = first;
        // Keep the public ReHLDS hookchain on every bounded resource message.
        SV_SendResources(&msg);
        first += std::min(goldcraft::precache::chunk_entries, total - first);
        Netchan_CreateFragments(TRUE, &client->netchan, &msg);
    } while (first < total);
    manifestClient = nullptr;
    manifestFirst = 0;
    Netchan_FragSend(&client->netchan);
}

bool GC_WriteResourceChunk(sizebuf_t* msg) {
        if (!manifestClient) return false;
        const auto total = static_cast<unsigned>(gc_precache.resources.size());
        const auto first = manifestFirst;
        if (!first) {
            MSG_WriteByte(msg, svc_resourcerequest);
            MSG_WriteLong(msg, g_psvs.spawncount);
            MSG_WriteLong(msg, 0);
            if (sv_downloadurl.string && sv_downloadurl.string[0] && Q_strlen(sv_downloadurl.string) < 129) {
                MSG_WriteByte(msg, svc_resourcelocation);
                MSG_WriteString(msg, sv_downloadurl.string);
            }
        }
        const auto count = std::min(goldcraft::precache::chunk_entries, total - first);
        MSG_WriteByte(msg, svc_resourcelist);
        MSG_StartBitWriting(msg);
        MSG_WriteBits(goldcraft::precache::marker, 12);
        MSG_WriteBits(goldcraft::precache::magic, 32);
        MSG_WriteBits(goldcraft::precache::version, 8);
        MSG_WriteBits(total, 32);
        MSG_WriteBits(first, 32);
        MSG_WriteBits(count, 16);
        const byte zero[32]{};
        for (unsigned i = first; i < first + count; ++i) {
            const auto& r = gc_precache.resources[i];
            MSG_WriteBits(r.type, 4);
            MSG_WriteBitString(r.szFileName);
            MSG_WriteBits(r.nIndex, 32);
            MSG_WriteBits(r.nDownloadSize, 24);
            MSG_WriteBits(r.ucFlags & (RES_WASMISSING | RES_FATALIFMISSING | RES_CUSTOM), 3);
            if (r.ucFlags & RES_CUSTOM) MSG_WriteBitData((void*)r.rgucMD5_hash, 16);
            const bool reserved = Q_memcmp(r.rguc_reserved, zero, sizeof(zero)) != 0;
            MSG_WriteBits(reserved, 1);
            if (reserved) MSG_WriteBitData((void*)r.rguc_reserved, sizeof(r.rguc_reserved));
        }
        if (first + count == total) SV_SendConsistencyList(msg);
        MSG_EndBitWriting(msg);
        return true;
}

void GC_PrecacheInit() {
    Cvar_RegisterVariable(&precacheVersion);
    Cmd_AddCommand("gc_precache_stats", PrecacheStats);
    Cmd_AddCommand("gc_precache_fixture_pad", PrecacheFixturePad);
}

void GC_WidenModelDelta(delta_t* delta) {
    for (int i = 0; i < delta->fieldCount; ++i) {
        auto& field = delta->pdd[i];
        // clientdata_t.viewmodel is the first-person weapon, separate from
        // entity_state_t.weaponmodel (the weapon held by another player).
        if (!Q_strcmp(field.fieldName, "modelindex") || !Q_strcmp(field.fieldName, "weaponmodel") ||
            !Q_strcmp(field.fieldName, "viewmodel"))
        {
            field.fieldType = DT_INTEGER;
            field.significant_bits = 32;
            field.premultiply = field.postmultiply = 1.0f;
        }
    }
}

void GC_WriteMediaIndex(sizebuf_t* message, int index) {
    if (index < 0) Host_Error("GoldCraft: negative media index %d", index);
    byte data[6];
    const unsigned count = goldcraft::precache::encode_media(data, unsigned(index));
    MSG_WriteBuf(message, count, data);
}

void GC_WriteSoundIndex(int index, int bits) {
    if (index < 0 || (bits != 8 && bits != 16)) Host_Error("GoldCraft: invalid sound index encoding");
    if (unsigned(index) < goldcraft::precache::media_escape) MSG_WriteBits(index, bits);
    else {
        MSG_WriteBits(goldcraft::precache::media_escape, 16);
        MSG_WriteBits(index, 32);
    }
}

void GC_MessageBegin() { messageExtraBytes = 0; }

bool GC_MessageMediaField() {
    if (gMsgBuffer.cursize < int(messageExtraBytes)) return false;
    return goldcraft::precache::message_media_field(gMsgType,
        gMsgBuffer.cursize ? gMsgBuffer.data[0] : 0, unsigned(gMsgBuffer.cursize) - messageExtraBytes);
}

bool GC_WriteMessageMedia(int index) {
    if (!GC_MessageMediaField()) return false;
    GC_WriteMediaIndex(&gMsgBuffer, index);
    messageExtraBytes += goldcraft::precache::media_bytes(unsigned(index)) - 2;
    return true;
}

model_t* GC_CachedModel(int index) { return &cachedModels.at(index)->model; }
mod_known_info_t* GC_ModelCRC(int index) { return &cachedModels.at(index)->crc; }
mod_known_info_t* GC_ModelCRC(model_t* model) { return &modelRecords.at(model)->crc; }
model_t* GC_NewCachedModel(qboolean trackCRC) {
    auto entry = std::unique_ptr<CachedModel>(new CachedModel{});
    entry->crc.shouldCRC = trackCRC;
    auto* model = &entry->model;
    modelRecords.emplace(model, entry.get());
    cachedModels.push_back(std::move(entry));
    mod_numknown = static_cast<int>(cachedModels.size());
    return model;
}
