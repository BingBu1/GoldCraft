#include "goldcraft/map_edits.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

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
    validate(s); Writer w; w.u64(s.epoch); w.u64(s.revision); w.u32(static_cast<std::uint32_t>(s.cuts.size()));
    for (const auto& c : s.cuts) { w.u64(c.id); write(w, c.target); write(w, c.box); } return w.data;
}
Bytes encode(const Delta& d) {
    validate(d); Writer w; w.u64(d.epoch); w.u64(d.base); w.u64(d.revision); w.u32(static_cast<std::uint32_t>(d.operation));
    if (d.operation != Operation::clear) write(w, d.target);
    if (d.operation == Operation::add) write(w, d.box);
    return w.data;
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
void Ledger::reset(std::uint64_t epoch) { state_ = {epoch, epoch ? 1u : 0u, {}}; journal_.clear(); }
void Ledger::swap(Ledger& other) noexcept { std::swap(state_,other.state_); journal_.swap(other.journal_); }
void Ledger::commit(Delta d) {
    validate(d);
    // Allocate journal storage before changing the authoritative state. Roll it
    // back if cut allocation fails, so peers never receive an uncommitted edit.
    journal_.push_back(d);
    try { apply(state_, d); } catch (...) { journal_.pop_back(); throw; }
    if (journal_.size() > journal_size) journal_.pop_front();
}
bool Ledger::add(Target t, Box b) {
    validate(t); validate(b);
    if (!state_.epoch) throw ProtocolError("No active map edit session");
    if (std::ranges::any_of(state_.cuts, [&](const Cut& c) { return c.target == t && c.box == b; })) return false;
    commit({state_.epoch, state_.revision, state_.revision + 1, Operation::add, t, b}); return true;
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
std::vector<Message> Ledger::since(std::uint64_t revision) const {
    if (!state_.epoch || revision == state_.revision) return {};
    std::vector<Message> messages;
    if (revision && revision < state_.revision && !journal_.empty() && revision >= journal_.front().base) {
        for (const auto& d : journal_) if (d.base >= revision) messages.push_back({Type::map_edit_delta, encode(d)});
    } else messages.push_back({Type::map_edit_snapshot, encode(state_)});
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
namespace {
void transfer_bounds(Type type, std::size_t total) {
    if ((type != Type::map_edit_snapshot && type != Type::map_edit_delta) || total < 20 ||
        total > (type == Type::map_edit_snapshot ? 20 + max_cuts * 44 : 64))
        throw ProtocolError("Map edit transfer bounds");
}
}
Bytes fragment(std::uint64_t epoch, std::uint64_t transfer, const Message& m, std::size_t offset) {
    transfer_bounds(m.type, m.payload.size());
    if (!epoch || !transfer || offset >= m.payload.size()) throw ProtocolError("Map edit fragment bounds");
    Writer w; w.u64(epoch); w.u64(transfer); w.u16(static_cast<std::uint16_t>(m.type));
    w.u32(static_cast<std::uint32_t>(m.payload.size())); w.u32(static_cast<std::uint32_t>(offset));
    w.bytes(std::span(m.payload).subspan(offset, std::min(fragment_bytes, m.payload.size() - offset))); return w.data;
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
