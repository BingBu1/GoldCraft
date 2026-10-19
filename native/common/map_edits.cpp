#include "goldcraft/map_edits.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace goldcraft::edits {
namespace {
void validate(Target t) {
    if (t.slot > 32767 || t.model > 4095 || (!t.slot && (t.serial || t.model)) || (t.slot && !t.model))
        throw ProtocolError("Map edit target bounds");
}
void validate(const Box& b) {
    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(b.min[i]) || !std::isfinite(b.max[i]) || b.min[i] >= b.max[i] ||
            b.min[i] < -32768 || b.max[i] > 32768)
            throw ProtocolError("Map edit volume bounds");
}
void validate(const Snapshot& s) {
    if (!s.epoch || !s.revision || s.cuts.size() > max_cuts) throw ProtocolError("Map edit snapshot bounds");
    std::uint64_t previous = 0;
    for (const auto& c : s.cuts) {
        if (c.id <= previous || c.id > s.revision) throw ProtocolError("Map edit identity order");
        validate(c.target); validate(c.box); previous = c.id;
    }
}
void validate(const Delta& d) {
    if (!d.epoch || !d.base || d.base == UINT64_MAX || d.revision != d.base + 1)
        throw ProtocolError("Map edit revision sequence");
    switch (d.operation) {
    case Operation::add: validate(d.box); [[fallthrough]];
    case Operation::remove_target: validate(d.target); break;
    case Operation::clear: break;
    default: throw ProtocolError("Map edit operation");
    }
}
void validate(const Batch& b) {
    if (!b.epoch || !b.base || b.cuts.empty() || b.cuts.size() > max_cuts ||
        b.base > UINT64_MAX - b.cuts.size() || b.revision != b.base + b.cuts.size())
        throw ProtocolError("Map edit batch bounds");
    auto id = b.base;
    for (const auto& c : b.cuts) {
        if (c.id != ++id) throw ProtocolError("Map edit batch identity order");
        validate(c.target); validate(c.box);
    }
}
void write(Writer& w, Target t) { w.u32(t.slot); w.u32(t.serial); w.u32(t.model); }
void write(Writer& w, const Box& b) { for (auto n : b.min) w.f32(n); for (auto n : b.max) w.f32(n); }
Target target(Reader& r) { return {r.u32(), r.u32(), r.u32()}; }
Box box(Reader& r) {
    Box b; for (auto& n : b.min) n = r.f32(); for (auto& n : b.max) n = r.f32(); return b;
}
void apply(Snapshot& s, const Delta& d) {
    // Callers validate first. vector::push_back provides the strong guarantee;
    // erase/clear and the revision assignment cannot allocate or fail.
    if (d.operation == Operation::add) {
        if (s.cuts.size() >= max_cuts) throw ProtocolError("Map edit capacity");
        s.cuts.push_back({d.revision, d.target, d.box});
    } else if (d.operation == Operation::remove_target) {
        std::erase_if(s.cuts, [&](const Cut& c) { return c.target == d.target; });
    } else s.cuts.clear();
    s.revision = d.revision;
}
}
Bytes encode(const Snapshot& s) {
    validate(s); Writer w; w.data.reserve(20 + s.cuts.size() * 44);
    w.u64(s.epoch); w.u64(s.revision); w.u32(static_cast<std::uint32_t>(s.cuts.size()));
    for (const auto& c : s.cuts) { w.u64(c.id); write(w, c.target); write(w, c.box); } return std::move(w.data);
}
Bytes encode(const Delta& d) {
    validate(d); Writer w; w.data.reserve(64);
    w.u64(d.epoch); w.u64(d.base); w.u64(d.revision); w.u32(static_cast<std::uint32_t>(d.operation));
    if (d.operation != Operation::clear) write(w, d.target);
    if (d.operation == Operation::add) write(w, d.box);
    return std::move(w.data);
}
Bytes encode(const Batch& b) {
    validate(b); Writer w; w.data.reserve(28 + b.cuts.size() * 44);
    w.u64(b.epoch); w.u64(b.base); w.u64(b.revision); w.u32(static_cast<std::uint32_t>(b.cuts.size()));
    for (const auto& c : b.cuts) { w.u64(c.id); write(w, c.target); write(w, c.box); }
    return std::move(w.data);
}
Snapshot snapshot(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Snapshot s; s.epoch = r.u64(); s.revision = r.u64(); const auto count = r.u32();
    if (count > max_cuts || r.remaining() != count * 44ull) throw ProtocolError("Map edit snapshot length");
    s.cuts.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) s.cuts.push_back({r.u64(), target(r), box(r)});
    r.finish(); validate(s); return s;
}
Delta delta(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Delta d; d.epoch = r.u64(); d.base = r.u64(); d.revision = r.u64();
    d.operation = static_cast<Operation>(r.u32());
    if (d.operation != Operation::clear) d.target = target(r);
    if (d.operation == Operation::add) d.box = box(r);
    r.finish(); validate(d); return d;
}
Batch batch(std::span<const std::uint8_t> bytes) {
    Reader r(bytes); Batch b; b.epoch = r.u64(); b.base = r.u64(); b.revision = r.u64(); const auto count = r.u32();
    if (!count || count > max_cuts || r.remaining() != count * 44ull) throw ProtocolError("Map edit batch length");
    b.cuts.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) b.cuts.push_back({r.u64(), target(r), box(r)});
    r.finish(); validate(b); return b;
}
void Ledger::reset(std::uint64_t epoch) {
    state_ = {epoch, epoch ? 1u : 0u, {}}; journal_.clear(); journal_bytes_ = 0; snapshot_message_.reset();
}
void Ledger::swap(Ledger& other) noexcept {
    std::swap(state_,other.state_); journal_.swap(other.journal_); snapshot_message_.swap(other.snapshot_message_);
    std::swap(journal_bytes_,other.journal_bytes_);
}
void Ledger::trim() noexcept {
    while (journal_.size() > journal_size || journal_bytes_ > journal_bytes) {
        journal_bytes_ -= journal_.front().bytes; journal_.pop_front();
    }
}
void Ledger::commit(Delta d) {
    validate(d);
    // Allocate journal storage before changing the authoritative state. Roll it
    // back if cut allocation fails, so peers never receive an uncommitted edit.
    journal_.push_back({d.base,d.revision,d,{},64});
    try { apply(state_, d); } catch (...) { journal_.pop_back(); throw; }
    snapshot_message_.reset();
    journal_bytes_ += 64; trim();
}
bool Ledger::add(Target t, Box b) {
    validate(t); validate(b);
    if (!state_.epoch) throw ProtocolError("No active map edit session");
    if (std::ranges::any_of(state_.cuts, [&](const Cut& c) { return c.target == t && c.box == b; })) return false;
    commit({state_.epoch, state_.revision, state_.revision + 1, Operation::add, t, b}); return true;
}
bool Ledger::add_batch(std::span<const Cut> cuts) {
    if (!state_.epoch || !state_.revision) throw ProtocolError("No active map edit session");
    using Identity = std::tuple<std::uint32_t,std::uint32_t,std::uint32_t,std::array<float,3>,std::array<float,3>>;
    auto key = [](const Cut& c) { return Identity{c.target.slot,c.target.serial,c.target.model,c.box.min,c.box.max}; };
    std::set<Identity> known;
    for (const auto& c : state_.cuts) known.insert(key(c));
    auto next = state_.cuts;
    auto revision = state_.revision;
    for (const auto& c : cuts) {
        validate(c.target); validate(c.box);
        if (!known.insert(key(c)).second) continue;
        if (next.size() == max_cuts || revision == UINT64_MAX) throw ProtocolError("Map edit capacity");
        next.push_back({++revision,c.target,c.box});
    }
    if (revision == state_.revision) return false;
    Batch change{state_.epoch,state_.revision,revision,{next.begin()+state_.cuts.size(),next.end()}};
    auto message = std::make_shared<const Message>(Message{Type::map_edit_batch,encode(change)});
    const auto bytes = message->payload.size();
    // Complete allocation/encoding before publishing state. In-flight peers
    // keep immutable messages even after eviction, a candidate swap or reset.
    journal_.push_back({change.base,change.revision,{},std::move(message),bytes});
    state_.cuts.swap(next); state_.revision = revision;
    snapshot_message_.reset();
    journal_bytes_ += bytes; trim();
    return true;
}
bool Ledger::remove(Target t) {
    validate(t);
    if (!std::ranges::any_of(state_.cuts, [&](const Cut& c) { return c.target == t; })) return false;
    commit({state_.epoch, state_.revision, state_.revision + 1, Operation::remove_target, t, {}}); return true;
}
bool Ledger::restore() {
    if (state_.cuts.empty()) return false;
    commit({state_.epoch, state_.revision, state_.revision + 1, Operation::clear, {}, {}}); return true;
}
std::shared_ptr<const Message> Ledger::next(std::uint64_t revision, bool batches) const {
    if (!state_.epoch || revision == state_.revision) return {};
    if (revision && revision < state_.revision) {
        const auto entry = std::lower_bound(journal_.begin(),journal_.end(),revision,
            [](const JournalEntry& e, std::uint64_t value) { return e.base < value; });
        // A revision inside a batch is not a checkpoint. Legacy peers and
        // missing/expired history recover through a complete snapshot.
        if (entry != journal_.end() && entry->base == revision &&
            (batches || !entry->message || entry->message->type != Type::map_edit_batch)) {
            if (!entry->message) entry->message = std::make_shared<const Message>(Message{Type::map_edit_delta, encode(entry->change)});
            return entry->message;
        }
    }
    if (!snapshot_message_) snapshot_message_ = std::make_shared<const Message>(Message{Type::map_edit_snapshot, encode(state_)});
    return snapshot_message_;
}
std::vector<Message> Ledger::since(std::uint64_t revision, bool batches) const {
    std::vector<Message> messages;
    while (auto message = next(revision,batches)) {
        Reader r(message->payload); r.u64(); revision = r.u64();
        if (message->type != Type::map_edit_snapshot) revision = r.u64();
        messages.push_back(*message);
    }
    return messages;
}
void Replica::reset(std::uint64_t epoch) { state_ = {epoch, 0, {}}; ready_ = false; }
Applied Replica::accept(const Snapshot& s) {
    validate(s);
    if (!state_.epoch || s.epoch != state_.epoch || s.revision < state_.revision || (ready_ && s.revision == state_.revision))
        return Applied::ignored;
    Snapshot next = s; state_ = std::move(next); ready_ = true; return Applied::changed;
}
Applied Replica::accept(const Delta& d) {
    validate(d);
    if (!state_.epoch || d.epoch != state_.epoch || d.revision <= state_.revision) return Applied::ignored;
    if (!ready_ || d.base != state_.revision) { ready_ = false; return Applied::need_snapshot; }
    apply(state_, d); return Applied::changed;
}
Applied Replica::accept(const Batch& b) {
    validate(b);
    if (!state_.epoch || b.epoch != state_.epoch || b.revision <= state_.revision) return Applied::ignored;
    if (!ready_ || b.base != state_.revision) { ready_ = false; return Applied::need_snapshot; }
    if (b.cuts.size() > max_cuts - state_.cuts.size()) throw ProtocolError("Map edit capacity");
    // Cut is trivially copyable; insertion provides the strong guarantee.
    state_.cuts.insert(state_.cuts.end(),b.cuts.begin(),b.cuts.end());
    state_.revision = b.revision; return Applied::changed;
}
namespace {
void transfer_bounds(Type type, std::size_t total) {
    const auto limit = type == Type::map_edit_snapshot ? 20 + max_cuts * 44 :
        type == Type::map_edit_batch ? 28 + max_cuts * 44 : 64;
    if ((type != Type::map_edit_snapshot && type != Type::map_edit_delta && type != Type::map_edit_batch) ||
        total < (type == Type::map_edit_batch ? 72u : 20u) || total > limit)
        throw ProtocolError("Map edit transfer bounds");
}
}
Bytes fragment(std::uint64_t epoch, std::uint64_t transfer, const Message& m, std::size_t offset) {
    transfer_bounds(m.type, m.payload.size());
    if (!epoch || !transfer || offset >= m.payload.size()) throw ProtocolError("Map edit fragment bounds");
    Writer w; w.data.reserve(26 + std::min(fragment_bytes, m.payload.size() - offset));
    w.u64(epoch); w.u64(transfer); w.u16(static_cast<std::uint16_t>(m.type));
    w.u32(static_cast<std::uint32_t>(m.payload.size())); w.u32(static_cast<std::uint32_t>(offset));
    w.bytes(std::span(m.payload).subspan(offset, std::min(fragment_bytes, m.payload.size() - offset))); return std::move(w.data);
}
void Assembler::reset(std::uint64_t epoch) { epoch_ = epoch; transfer_ = total_ = 0; pending_.clear(); }
std::optional<Message> Assembler::accept(std::span<const std::uint8_t> bytes) {
    try {
        Reader r(bytes); const auto epoch = r.u64(), transfer = r.u64(); const auto type = static_cast<Type>(r.u16());
        const auto total = r.u32(), offset = r.u32(); const auto size = r.remaining();
        transfer_bounds(type, total);
        if (!transfer || !size || size > fragment_bytes || offset > total || size > total - offset)
            throw ProtocolError("Map edit fragment bounds");
        if (!epoch_ || epoch != epoch_ || transfer < transfer_) return std::nullopt;
        if (transfer == transfer_ && !total_) return std::nullopt;
        if (offset == 0 && transfer > transfer_) {
            Bytes next; next.reserve(total); pending_ = std::move(next);
            transfer_ = transfer; type_ = type; total_ = total;
        } else if (transfer == transfer_ && offset < pending_.size()) return std::nullopt;
        if (transfer != transfer_ || type != type_ || total != total_ || offset != pending_.size())
            throw ProtocolError("Map edit fragment sequence");
        const auto data = r.bytes(size); pending_.insert(pending_.end(), data.begin(), data.end());
        if (pending_.size() != total_) return std::nullopt;
        Message result{type_, std::move(pending_)};
        // Retain transfer identity while marking it consumed. Duplicate complete
        // packets cannot become a second state transition.
        total_ = 0; return result;
    } catch (...) { pending_.clear(); total_ = 0; throw; }
}
} // namespace goldcraft::edits
