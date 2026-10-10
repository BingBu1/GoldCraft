#include "goldcraft/wire.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace goldcraft {
void Writer::u8(std::uint8_t v) { data.push_back(v); }
void Writer::u16(std::uint16_t v) { for (int i = 0; i < 2; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
void Writer::u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
void Writer::i32(std::int32_t v) { u32(std::bit_cast<std::uint32_t>(v)); }
void Writer::u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
void Writer::f32(float v) { if (!std::isfinite(v)) throw ProtocolError("non-finite float"); u32(std::bit_cast<std::uint32_t>(v)); }
void Writer::bytes(std::span<const std::uint8_t> v) { data.insert(data.end(), v.begin(), v.end()); }
void Writer::string(const std::string& v) {
    if (v.size() > 65535) throw ProtocolError("string too long");
    u16(static_cast<std::uint16_t>(v.size()));
    bytes({ reinterpret_cast<const std::uint8_t*>(v.data()), v.size() });
}
std::span<const std::uint8_t> Reader::bytes(std::size_t size) {
    if (size > remaining()) throw ProtocolError("truncated payload");
    auto result = data_.subspan(position_, size); position_ += size; return result;
}
std::uint8_t Reader::u8() { return bytes(1)[0]; }
std::uint16_t Reader::u16() { auto b = bytes(2); return static_cast<std::uint16_t>(b[0] | (std::uint16_t(b[1]) << 8)); }
std::uint32_t Reader::u32() { auto b = bytes(4); std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= std::uint32_t(b[i]) << (i * 8); return v; }
std::int32_t Reader::i32() { return std::bit_cast<std::int32_t>(u32()); }
std::uint64_t Reader::u64() { auto b = bytes(8); std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= std::uint64_t(b[i]) << (i * 8); return v; }
float Reader::f32() { float v = std::bit_cast<float>(u32()); if (!std::isfinite(v)) throw ProtocolError("non-finite float"); return v; }
Key Reader::key() { Key k{}; auto b = bytes(k.size()); std::copy(b.begin(), b.end(), k.begin()); return k; }
std::string Reader::string(std::size_t limit) { auto n = u16(); if (n > limit) throw ProtocolError("string limit"); auto b = bytes(n); return { b.begin(), b.end() }; }
void Reader::finish() const { if (remaining()) throw ProtocolError("trailing bytes"); }

Key parse_key(const std::string& hex) {
    if (hex.size() != 32) throw ProtocolError("session/token must contain 32 hexadecimal characters");
    auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw ProtocolError("invalid hexadecimal key");
    };
    Key key{}; for (std::size_t i = 0; i < key.size(); ++i) key[i] = static_cast<std::uint8_t>((digit(hex[2*i]) << 4) | digit(hex[2*i+1])); return key;
}
Key random_key() {
    Key key{};
    if (BCryptGenRandom(nullptr, key.data(), static_cast<ULONG>(key.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw ProtocolError("system RNG failed");
    return key;
}
std::uint64_t random_epoch() { auto key = random_key(); Reader r(key); return r.u64() | 1; }

bool known_type(Type t) {
    switch (t) {
    case Type::hello: case Type::welcome: case Type::heartbeat: case Type::world: case Type::actors:
    case Type::client_binding: case Type::pair_player: case Type::pair_result: case Type::input:
    case Type::player_pose: case Type::atlas: case Type::section_mesh: case Type::remove_section:
    case Type::bsp: case Type::brushes: case Type::control: case Type::lights:
    case Type::entity_texture: case Type::entity_mesh: case Type::trace_query: case Type::trace_result:
    case Type::scene_reset: case Type::atlas_patches:
    case Type::hud_frame: case Type::viewport: case Type::ui_input:
    case Type::particle_texture: case Type::particle_mesh:
    case Type::block_feedback: case Type::camera: case Type::key_input:
    case Type::authoritative_pose: case Type::minecraft_objects: case Type::object_action: case Type::host_entities:
    case Type::damage_request: case Type::damage_result: case Type::vitals_delta:
    case Type::map_mining_policy: case Type::map_mining_request: case Type::map_mining_result:
    case Type::map_edit_snapshot: case Type::map_edit_delta: case Type::map_edit_query:
    case Type::map_mining_sample: case Type::map_mining_surface: return true;
    default: return false;
    }
}
Header decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != header_bytes) throw ProtocolError("invalid header size");
    Reader r(bytes);
    if (r.u32() != magic || r.u16() != protocol_version) throw ProtocolError("protocol magic/version mismatch");
    Header h{}; h.type = static_cast<Type>(r.u16()); h.payload_bytes = r.u32();
    if (r.u32() != 0) throw ProtocolError("unknown header flags");
    h.epoch = r.u64(); h.sequence = r.u64(); r.finish();
    if (!known_type(h.type) || h.payload_bytes > max_payload || h.sequence == 0) throw ProtocolError("invalid frame header");
    return h;
}
Bytes encode_frame(Type type, std::uint64_t epoch, std::uint64_t sequence, std::span<const std::uint8_t> payload) {
    if (!known_type(type) || payload.size() > max_payload || sequence == 0) throw ProtocolError("invalid outbound frame");
    Writer w; w.data.reserve(header_bytes + payload.size());
    w.u32(magic); w.u16(protocol_version); w.u16(static_cast<std::uint16_t>(type)); w.u32(static_cast<std::uint32_t>(payload.size()));
    w.u32(0); w.u64(epoch); w.u64(sequence); w.bytes(payload); return w.data;
}

HostHandshake::HostHandshake(Key s, Key t, Role h, Role p, std::uint64_t e) : session_(s), token_(t), host_(h), peer_(p), epoch_(e) {
    if (!e || !((h == Role::host_client && p == Role::fabric_client) || (h == Role::host_server && p == Role::fabric_server))) throw ProtocolError("invalid endpoint role/epoch");
}
void HostHandshake::accept(const Header& h, std::span<const std::uint8_t> payload) {
    if (payload.size() != h.payload_bytes || last_sequence_ == std::numeric_limits<std::uint64_t>::max() || h.sequence != last_sequence_ + 1) throw ProtocolError("duplicate or skipped sequence");
    if (!ready_) {
        if (h.type != Type::hello || h.epoch != 0) throw ProtocolError("hello required");
        Reader r(payload); auto session = r.key(); auto token = r.key(); auto role = r.u32(); auto reserved = r.u32(); r.finish();
        unsigned difference = 0;
        for (std::size_t i = 0; i < 16; ++i) difference |= (session[i] ^ session_[i]) | (token[i] ^ token_[i]);
        if (difference || role != static_cast<std::uint32_t>(peer_) || reserved) throw ProtocolError("pairing credentials/role mismatch");
        ready_ = true;
    } else {
        if (h.epoch != epoch_ || h.type == Type::hello || h.type == Type::welcome) throw ProtocolError("stale connection epoch or repeated handshake");
        if (h.type == Type::heartbeat && !payload.empty()) throw ProtocolError("invalid heartbeat");
    }
    last_sequence_ = h.sequence;
}
Bytes HostHandshake::welcome() const {
    Writer w; w.bytes(session_); w.u32(static_cast<std::uint32_t>(host_)); w.u32(0); w.f32(units_per_block); w.f32(minecraft_y_offset); return w.data;
}
Vec3 to_minecraft(Vec3 v) { return { v.x / units_per_block, v.z / units_per_block + minecraft_y_offset, -v.y / units_per_block }; }
Vec3 to_goldsrc(Vec3 v) { return { v.x * units_per_block, -v.z * units_per_block, (v.y - minecraft_y_offset) * units_per_block }; }
std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t value=0xffffffffu;
    for(auto byte:bytes){value^=byte;for(int bit=0;bit<8;++bit)value=(value>>1)^(0xedb88320u&(0u-(value&1u)));}
    return ~value;
}
}
