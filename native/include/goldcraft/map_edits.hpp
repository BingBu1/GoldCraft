#pragma once
#include "goldcraft/wire.hpp"
#include <deque>
#include <optional>

namespace goldcraft::edits {
// Model-local GoldSrc coordinates; no native pointers or engine layouts cross
// the wire. A reused edict or a different inline model is a different target.
struct Target {
    std::uint32_t slot = 0, serial = 0, model = 0;
    bool operator==(const Target&) const = default;
};
struct Box {
    std::array<float, 3> min{}, max{};
    bool operator==(const Box&) const = default;
};
struct Cut {
    std::uint64_t id = 0;
    Target target;
    Box box;
    bool operator==(const Cut&) const = default;
};
struct Snapshot {
    std::uint64_t epoch = 0, revision = 0;
    std::vector<Cut> cuts;
    bool operator==(const Snapshot&) const = default;
};
enum class Operation : std::uint32_t { add = 1, remove_target = 2, clear = 3 };
struct Delta {
    std::uint64_t epoch = 0, base = 0, revision = 0;
    Operation operation = Operation::clear;
    Target target;
    Box box;
};
constexpr std::size_t max_cuts = 65536, journal_size = 256;
Bytes encode(const Snapshot& snapshot);
Bytes encode(const Delta& delta);
Snapshot snapshot(std::span<const std::uint8_t> bytes);
Delta delta(std::span<const std::uint8_t> bytes);

// Owned by the native map session, never by an IPC connection. Geometry users
// must prepare rendering/physics successfully before committing a cut here.
class Ledger {
public:
    void reset(std::uint64_t epoch);
    bool add(Target target, Box box);
    bool remove(Target target);
    bool restore();
    const Snapshot& state() const { return state_; }
    // Missing/expired history returns a complete snapshot. A disconnected
    // authority can recover even after the bounded delta journal has rolled.
    std::vector<Message> since(std::uint64_t revision) const;
private:
    void commit(Delta change);
    Snapshot state_;
    std::deque<Delta> journal_;
};

enum class Applied { ignored, changed, need_snapshot };
class Replica {
public:
    void reset(std::uint64_t epoch);
    Applied accept(const Snapshot& snapshot);
    Applied accept(const Delta& delta);
    void invalidate() { ready_ = false; }
    const Snapshot& state() const { return state_; }
    bool ready() const { return ready_; }
private:
    Snapshot state_;
    bool ready_ = false;
};

// GoldSrc user messages are limited to 192 bytes. Reliable delivery is ordered,
// but a fresh snapshot may explicitly replace a transfer after resynchronizing.
constexpr std::size_t fragment_bytes = 160;
Bytes fragment(std::uint64_t epoch, std::uint64_t transfer, const Message& message, std::size_t offset);
class Assembler {
public:
    void reset(std::uint64_t epoch);
    std::optional<Message> accept(std::span<const std::uint8_t> bytes);
private:
    std::uint64_t epoch_ = 0, transfer_ = 0;
    Type type_ = Type::map_edit_snapshot;
    std::size_t total_ = 0;
    Bytes pending_;
};
} // namespace goldcraft::edits
