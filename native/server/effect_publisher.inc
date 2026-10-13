// Included after effect_host.inc in the bridge's owning-thread namespace.
// This owns execution grants, not registry effects or the native movement lease.
class EffectPublisher final {
    using Identity = HostEffects::ExecutionIdentity;
    using Sample = HostEffects::ExecutionSample;
    using Grant = goldcraft::effects::Grant;
    using Proof = goldcraft::effects::PoseProof;
    using Status = goldcraft::amxx_effects::ServiceStatus;
    static constexpr std::uint32_t freshness_ms = 750;
    static constexpr std::uint64_t renewal_ms = 100;
    static constexpr std::size_t history_size = 16;
    struct Slot {
        Identity identity{};
        std::uint64_t stream = 0, lastIssued = 0, deadline = 0;
        bool admitted = false;
        std::size_t next = 0, count = 0;
        std::array<Grant, history_size> history{};
    };
    HostEffects& effects_;
    std::array<Slot, 65> slots_{};
    // Never reset on map, connection, life, form or failed send. Stopping in the
    // signed-positive range also permits consumers using ordinary long order.
    std::uint64_t next_stream_, next_publication_;

    static std::uint64_t Allocate(std::uint64_t& next) noexcept {
        if (!next || next > static_cast<std::uint64_t>(INT64_MAX)) return 0;
        const auto value = next;
        next = value == static_cast<std::uint64_t>(INT64_MAX) ? 0 : value + 1;
        return value;
    }
    static bool SameCoordinate(float a, float b) noexcept {
        // Four single-precision rounding steps at map scale. The floor covers
        // MC's vertical-offset subtraction near zero without gameplay slack.
        const float scale = std::max({1.0f, std::abs(a), std::abs(b)});
        return std::isfinite(a) && std::isfinite(b) &&
            std::abs(a - b) <= std::max(0.00025f, 4.0f * std::numeric_limits<float>::epsilon() * scale);
    }
    static bool Supported(const goldcraft::effects::MovementState& state) noexcept {
        // Reject the whole aggregate; reducing unsupported flags would execute
        // a different effect than Pawn registered.
        return goldcraft::effects::valid_movement(state) &&
            (state.capability == 0 || state.capability == goldcraft::effects::position_hold) &&
            !state.inputLocks && !state.wishSpeedCap && state.gravityFactor == 1.0f;
    }
    static bool Channel() noexcept {
        return link.connected() && link.effect_position_hold() && link.local_qpc_frequency() &&
            connection_generation && connection_generation == link.generation();
    }
    bool Read(const Identity& identity, Sample& output, bool& bootstrap) noexcept {
        const auto slot = static_cast<unsigned>(identity.target.key);
        if (!slot || slot >= controlled.size() || !Channel()) return false;
        bootstrap = !controlled[slot].active;
        if (bootstrap) {
            HostEffects::BootstrapSample sample;
            if (effects_.ReadBootstrapSample(identity, freshness_ms, &sample) != Status::ok) return false;
            output = {sample.identity, sample.constraints, sample.frequency, sample.issuedAtMs,
                      sample.deadlineMs, sample.checkedAtMs, sample.nativeFeet, sample.nativeAnchorFeet};
        } else if (effects_.ReadExecutionSample(identity, freshness_ms, &output) != Status::ok) return false;
        return Channel() && output.frequency == link.local_qpc_frequency() &&
            output.identity == identity && Supported(output.constraints.movement);
    }
    static bool SameSemantics(const Grant& grant, const Sample& sample) noexcept {
        if (grant.constraints != sample.constraints || grant.frequency != sample.frequency) return false;
        const auto held = sample.constraints.movement.holdAxes;
        for (unsigned axis = 0; axis < sample.nativeFeet.size(); ++axis)
            if ((held & (std::uint32_t{1} << axis)) &&
                !SameCoordinate(grant.nativeAnchorFeet[axis], sample.nativeAnchorFeet[axis])) return false;
        return true;
    }
    const Grant* Find(const Slot& state, std::uint64_t publication) const noexcept {
        for (std::size_t index = 0; index < state.count; ++index)
            if (state.history[index].publication == publication) return &state.history[index];
        return nullptr;
    }
    void Publish(unsigned slot, const Identity& identity, const Sample& sample, bool bootstrap) {
        auto& state = slots_[slot];
        if (state.identity != identity || (state.deadline && sample.checkedAtMs >= state.deadline))
            state = {};
        if (!state.stream) {
            state.identity = identity;
            state.stream = Allocate(next_stream_);
            if (!state.stream) return;
        }
        if (state.count) {
            const auto& latest = state.history[(state.next + history_size - 1) % history_size];
            if (sample.checkedAtMs < state.lastIssued) { state = {}; return; }
            if (sample.checkedAtMs - state.lastIssued < renewal_ms &&
                SameSemantics(latest, sample) && latest.bootstrap == bootstrap) return;
        }
        Grant grant;
        grant.operation = state.admitted ? goldcraft::effects::GrantOperation::update : goldcraft::effects::GrantOperation::open;
        grant.serviceGeneration = identity.context.generation;
        grant.nativeConnection = identity.authorityConnection;
        grant.constraints = sample.constraints;
        grant.minecraftUuid = identity.minecraftUuid;
        grant.stream = state.stream;
        grant.publication = Allocate(next_publication_);
        if (!grant.publication) { state = {}; return; }
        grant.frequency = sample.frequency;
        grant.issuedAtMs = sample.issuedAtMs;
        grant.deadlineMs = sample.deadlineMs;
        grant.bootstrap = bootstrap;
        grant.nativeFeet = sample.nativeFeet;
        grant.nativeAnchorFeet = sample.nativeAnchorFeet;
        if (!goldcraft::effects::valid_grant(grant)) { state = {}; return; }
        // A failed queue operation consumes the number but creates no admissible
        // history entry. Do not rewrite the latest successfully queued lease.
        if (!link.send(goldcraft::Type::effect_grant, goldcraft::effects::encode_grant(grant))) return;
        state.history[state.next] = grant;
        state.next = (state.next + 1) % history_size;
        state.count = std::min(state.count + 1, history_size);
        state.lastIssued = sample.issuedAtMs;
        state.deadline = sample.deadlineMs;
    }

public:
    explicit EffectPublisher(HostEffects& effects = host_effects,
                             std::uint64_t firstStream = 1, std::uint64_t firstPublication = 1) noexcept
        : effects_(effects), next_stream_(firstStream), next_publication_(firstPublication) {}
    void Reset() noexcept {
        for (unsigned slot = 1; slot < slots_.size(); ++slot) Retire(slot);
    }
    void Retire(unsigned slot) noexcept {
        // Legacy/cap0 and ordinary unpaired slots pay no repeated large ring
        // clear on every host frame. Only a previously opened slot has state.
        if (slot < slots_.size() && slots_[slot].identity.context.generation) slots_[slot] = {};
    }
    void Frame() {
        if (!edicts || !Channel()) { Reset(); return; }
        goldcraft::amxx_effects::ServiceContext context;
        if (effects_.GetContext(&context) != Status::ok) { Reset(); return; }
        for (unsigned slot = 1; slot < slots_.size(); ++slot) {
            const auto* pair = registry.get(slot);
            if (slot > static_cast<unsigned>(max_clients) || !pair || !pair->paired ||
                !minecraft_forms[slot] || !player_lives[slot]) { Retire(slot); continue; }
            const Identity identity{context,
                {context.world, goldcraft::effects::TargetKind::player, slot, pair->serial, player_lives[slot]},
                pair->uuid, connection_generation};
            Sample sample;
            bool bootstrap = false;
            if (!Read(identity, sample, bootstrap)) { Retire(slot); continue; }
            Publish(slot, identity, sample, bootstrap);
        }
    }
    void Reopen(const goldcraft::effects::Reopen& request) noexcept {
        if (!Channel() || !request.slot || request.slot >= slots_.size()) return;
        const auto& state = slots_[request.slot];
        const auto& identity = state.identity;
        if (request.world == identity.context.world && request.serial == identity.target.serial &&
            request.life == identity.target.life && request.minecraftUuid == identity.minecraftUuid &&
            request.serviceGeneration == identity.context.generation &&
            request.nativeConnection == identity.authorityConnection &&
            request.nativeConnection == connection_generation && request.stream && request.stream == state.stream)
            Retire(request.slot);
    }
    bool Admit(unsigned slot, const Proof& proof, const std::array<float, 3>& submitted) noexcept {
        if (!Channel() || !slot || slot >= slots_.size() || proof.empty()) return false;
        auto& state = slots_[slot];
        if (!state.stream || proof.stream != state.stream ||
            proof.serviceGeneration != state.identity.context.generation ||
            proof.nativeConnection != state.identity.authorityConnection ||
            proof.nativeConnection != connection_generation) return false;
        const Grant* grant = Find(state, proof.publication);
        if (!grant) return false;
        Sample current;
        bool bootstrap = false;
        if (!Read(state.identity, current, bootstrap)) { Retire(slot); return false; }
        if (current.checkedAtMs >= state.deadline) { Retire(slot); return false; }
        // Older publications are valid while their own lease and current exact
        // semantics remain live. Requiring the latest publication starves RTT.
        if (current.checkedAtMs < grant->issuedAtMs || current.checkedAtMs >= grant->deadlineMs ||
            !SameSemantics(*grant, current)) return false;
        const auto held = current.constraints.movement.holdAxes;
        for (unsigned axis = 0; axis < submitted.size(); ++axis) {
            if (!std::isfinite(submitted[axis]) || std::abs(submitted[axis]) > 16384.0f) return false;
            if (held & (std::uint32_t{1} << axis)) {
                if (!SameCoordinate(submitted[axis], grant->nativeAnchorFeet[axis])) return false;
            } else if (!state.admitted &&
                (!SameCoordinate(submitted[axis], grant->nativeFeet[axis]) ||
                 !SameCoordinate(current.nativeFeet[axis], grant->nativeFeet[axis]))) {
                // Placement/ACK was for an obsolete native spawn or movement.
                // New OPEN must carry a new stream; never acquire from it.
                Retire(slot);
                return false;
            }
        }
        state.admitted = true;
        return true; // Caller must perform physical writes now, without hooks.
    }
};

EffectPublisher effect_publisher;
