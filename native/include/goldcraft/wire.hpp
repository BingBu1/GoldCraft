#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace goldcraft {
using Bytes = std::vector<std::uint8_t>;
using Key = std::array<std::uint8_t, 16>;
constexpr std::uint32_t magic = 0x31464347; // GCF1, little endian
constexpr std::uint16_t protocol_version = 19;
constexpr std::size_t header_bytes = 32;
constexpr std::size_t max_payload = 32 * 1024 * 1024;
constexpr std::size_t max_queued_bytes = 64 * 1024 * 1024;
constexpr float units_per_block = 32.0f;
constexpr float minecraft_y_offset = 64.0f;

enum class Role : std::uint32_t { host_client = 1, fabric_client = 2, host_server = 3, fabric_server = 4 };
enum class Type : std::uint16_t {
    hello = 1, welcome = 2, heartbeat = 3,
    world = 10, actors = 11, client_binding = 12, pair_player = 13, pair_result = 14,
    bsp = 15, brushes = 16,
    input = 20, player_pose = 21, atlas = 22, section_mesh = 23, remove_section = 24,
    control = 25, lights = 26, entity_texture = 27, entity_mesh = 28, scene_reset = 29,
    damage_request = 30, damage_result = 31, atlas_patches = 32,
    hud_frame = 33, viewport = 34, ui_input = 35,
    particle_texture = 36, particle_mesh = 37,
    block_feedback = 38, camera = 39, key_input = 55,
    trace_query = 40, trace_result = 41, authoritative_pose = 50,
    minecraft_objects = 51, object_action = 52, host_entities = 53, vitals_delta = 54,
    map_mining_policy = 56, map_mining_request = 57, map_mining_result = 58
};

class ProtocolError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

class Writer {
public:
    Bytes data;
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void i32(std::int32_t value);
    void u64(std::uint64_t value);
    void f32(float value);
    void bytes(std::span<const std::uint8_t> value);
    void string(const std::string& value);
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::int32_t i32();
    std::uint64_t u64();
    float f32();
    Key key();
    std::span<const std::uint8_t> bytes(std::size_t size);
    std::string string(std::size_t limit = 1024);
    std::size_t remaining() const { return data_.size() - position_; }
    void finish() const;
private:
    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
};

struct Header {
    Type type;
    std::uint32_t payload_bytes;
    std::uint64_t epoch;
    std::uint64_t sequence;
};
struct Message { Type type; Bytes payload; };
Key parse_key(const std::string& hex);
Key random_key();
std::uint64_t random_epoch();
Header decode_header(std::span<const std::uint8_t> bytes);
Bytes encode_frame(Type type, std::uint64_t epoch, std::uint64_t sequence, std::span<const std::uint8_t> payload);
bool known_type(Type type);
std::uint32_t crc32(std::span<const std::uint8_t> bytes);

// State belongs to one TCP connection. A reconnect gets a new epoch and sequence.
class HostHandshake {
public:
    HostHandshake(Key session, Key token, Role host, Role peer, std::uint64_t epoch);
    void accept(const Header& header, std::span<const std::uint8_t> payload);
    Bytes welcome() const;
    bool ready() const { return ready_; }
    std::uint64_t epoch() const { return epoch_; }
private:
    Key session_, token_;
    Role host_, peer_;
    std::uint64_t epoch_, last_sequence_ = 0;
    bool ready_ = false;
};

struct Vec3 { float x, y, z; };
Vec3 to_minecraft(Vec3 goldsrc);
Vec3 to_goldsrc(Vec3 minecraft);
}
