#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>

namespace goldcraft::steam_async {
using Clock = std::chrono::steady_clock;
using SteamId = std::uint64_t;
enum class VoiceResult : int { ok, not_initialized, not_recording, no_data,
    buffer_too_small, data_corrupted, restricted, unsupported_codec, receiver_out_of_date,
    receiver_did_not_answer };
inline constexpr std::size_t avatar_capacity = 128;
inline constexpr std::size_t image_bytes = 184 * 184 * 4;
inline constexpr std::size_t voice_bytes = 64 * 1024;
inline constexpr std::size_t voice_capacity = 16;
inline constexpr int image_base = 0x60000000;

struct Avatar {
    SteamId id{};
    unsigned size{}, width{}, height{};
    int relationship{}, handle{};
    std::array<std::uint8_t,image_bytes> rgba{};
    Clock::time_point updated{};
};
struct VoicePacket {
    unsigned generation{}, compressed{}, pcm{}, rate{};
    Clock::time_point captured{};
    std::array<std::uint8_t,voice_bytes> data{}, raw{};
};

// Only the workers call this interface. The production implementation owns an
// isolated Steam helper and cancellable IPC; engine/GL/VGUI pointers never enter
// these calls. Steam pipe creation/release happens only in the helper process.
struct Backend {
    virtual ~Backend() = default;
    virtual bool initialize() = 0;
    virtual void avatar(Avatar&) = 0;
    virtual void start_voice() = 0;
    virtual void stop_voice() = 0;
    virtual VoiceResult capture(VoicePacket&, bool pcm, unsigned rate) = 0;
};

struct Counters {
    std::atomic<unsigned> avatar_requests{}, avatar_completed{}, voice_packets{},
        dropped{}, lock_misses{}, last_voice_result{}, avatar_ready{}, voice_ready{};
};

// The main thread only uses try_lock and copied, completed results. A worker
// never owns this mutex while calling Steam. Capacity and memory are bounded.
class State {
public:
    std::atomic<bool> stopping{};
    Counters counters;
    int avatar(SteamId id, unsigned size);
    int relationship(SteamId id);
    bool image_size(int handle, unsigned* width, unsigned* height);
    bool image_rgba(int handle, void* data, int bytes);
    void start_voice();
    void stop_voice();
    VoiceResult available(unsigned* compressed, unsigned* pcm, unsigned rate);
    VoiceResult voice(bool compressed, void* data, unsigned capacity, unsigned* written,
        bool pcm, void* raw, unsigned raw_capacity, unsigned* raw_written, unsigned rate);
    void run_avatars(Backend&);
    void run_voice(Backend&);
private:
    struct Entry { SteamId id{}; unsigned size{}, revision{}; bool pending{};
        std::shared_ptr<const Avatar> value; Clock::time_point requested{}; };
    std::mutex mutex_;
    std::array<Entry,avatar_capacity> avatars_{};
    // Main-thread snapshots keep a valid image available even when a worker
    // is publishing another result. Image reads need no worker lock at all.
    std::array<std::shared_ptr<const Avatar>,avatar_capacity> client_avatars_{};
    std::array<std::unique_ptr<VoicePacket>,voice_capacity> packets_{};
    unsigned next_image_{}, revision_{}, read_{}, count_{};
    std::atomic<unsigned> desired_{}, pcm_rate_{}, drained_generation_{};
    std::atomic<bool> voice_failed_{};
    int request_avatar(SteamId, unsigned, bool relationship_only);
    void discard_old(Clock::time_point now, unsigned generation);
};
}
