// Included inside bridge.cpp's anonymous namespace after its actual authority
// globals are defined. This is the engine adapter, not a second identity source.
// Registration is state only; the accepted-move/physical execution path is still
// required before these declarations can be advertised as gameplay effects.
using goldcraft::amxx_effects::IEffectService;

class HostEffects final : public IEffectService {
    using Status = goldcraft::amxx_effects::ServiceStatus;
    using Context = goldcraft::amxx_effects::ServiceContext;
    using Target = goldcraft::effects::Target;
    using Kind = goldcraft::effects::TargetKind;
    using State = goldcraft::effects::MovementState;
    goldcraft::amxx_effects::EffectServiceProvider provider_;
    Context context_{};
    goldcraft::effect_clock::Clock clock_;
    bool clock_failed_ = false;
    std::array<Target, 65> registered_{};
    std::array<bool, 65> spawned_{};
    std::array<bool, 65> connected_{};
    std::array<std::uint64_t, 65> slot_epochs_{};
    struct Transition {
        std::uint32_t depth = 0, terminals = 0;
        bool spawn_pending = false;
    };
    std::array<Transition, 65> transitions_{};
    bool active_ = false;

    // Owning game thread only. Observe each aggregate transition before and
    // after mutations; a periodic publisher cannot reconstruct a release and
    // reapply that happened between its reads.
    struct AnchorRecord {
        Target target{};
        goldcraft::effects::Snapshot snapshot{};
        std::array<float, 3> feet{};
        std::uint32_t validAxes = 0;
        bool fault = false;
    };
    std::array<AnchorRecord, 65> anchors_{};

    void RefreshAnchors(std::uint64_t now) noexcept {
        for (unsigned slot = 1; slot < registered_.size(); ++slot) {
            const auto& target = registered_[slot];
            auto& record = anchors_[slot];
            if (!target.world) { record = {}; continue; }
            if (record.target != target) { record = {}; record.target = target; }
            if (record.fault) continue;
            goldcraft::effects::Snapshot snapshot;
            // Same qualified timestamp: no new clock read, Gate, allocation or
            // engine callback. ReadSnapshot may only pump the already-seen time.
            if (!Current(target) ||
                provider_.ReadSnapshot(context_, target, now, &snapshot) != Status::ok ||
                snapshot.target != target || !snapshot.revision ||
                !goldcraft::effects::valid_movement(snapshot.movement)) {
                record.fault = true;
                record.validAxes = 0;
                continue;
            }
            const auto held = snapshot.movement.holdAxes;
            // First admission must observe the empty state. A missing history
            // must never seed an already-held axis from a later drifted pose.
            if (record.snapshot.target != target && held) {
                record.fault = true;
                record.validAxes = 0;
                continue;
            }
            const auto acquired = held & ~record.snapshot.movement.holdAxes;
            record.validAxes &= held;
            if (acquired) {
                const auto& vars = edicts[slot].v;
                const std::array<float, 3> feet{vars.origin.x, vars.origin.y, vars.origin.z + vars.mins.z};
                for (unsigned axis = 0; axis < feet.size(); ++axis) {
                    const auto bit = std::uint32_t{1} << axis;
                    if ((acquired & bit) && std::isfinite(feet[axis])) {
                        record.feet[axis] = feet[axis];
                        record.validAxes |= bit;
                    }
                }
            }
            // Even a nonfinite capture records the transition: a renewal may
            // not repair it by taking a later position. Release/reapply may.
            record.snapshot = snapshot;
        }
    }

    bool Current(const Target& target) const noexcept {
        if (!edicts || !registry.world() || target.world != registry.world() ||
            target.kind != Kind::player || !target.key || target.key > static_cast<unsigned>(max_clients))
            return false;
        const auto slot = static_cast<unsigned>(target.key);
        if (!connected_[slot] || !spawned_[slot] || transitions_[slot].depth) return false;
        const auto* binding = registry.get(slot);
        if (!binding || binding->serial != target.serial || player_lives[slot] != target.life)
            return false;
        auto* entity = edicts + slot;
        if (entity->free || (entity->v.flags & FL_KILLME)) return false;
        auto* player = static_cast<CBasePlayer*>(GET_PRIVATE(entity));
        // CBasePlayer::IsAlive is virtual and can be replaced by Ham_IsAlive.
        // Match its real deadflag/health predicate without dispatching Pawn from
        // an API guard; recursive gc_effect_* calls must not enter this query.
        return player && entity->v.deadflag == DEAD_NO && entity->v.health > 0 &&
            (player->m_iTeam == TERRORIST || player->m_iTeam == CT) && !entity->v.iuser1;
    }

    void Failed(const char* operation, Status status) noexcept {
        // Shut the external gate first. Even an unexpected provider failure must
        // not expose old targets through the engine factory on the next API call.
        active_ = false;
        registered_ = {};
        anchors_ = {};
        spawned_ = {};
        connected_ = {};
        slot_epochs_ = {};
        transitions_ = {};
        provider_.AbortWorld(); // Never invent a timestamp during emergency shutdown.
        ALERT(at_console, "[GoldCraft] effect service stopped at %s, status=%u\n",
              operation, static_cast<unsigned>(status));
    }

    Status SampleTime(goldcraft::effect_clock::Sample& sample) noexcept {
        if (clock_failed_) return Status::clock_failed;
        if (clock_.read(sample) == goldcraft::effect_clock::Error::none &&
            provider_.ObserveClock(sample.raw) == Status::ok) return Status::ok;
        clock_failed_ = true;
        provider_.ClockFault();
        Failed("QPC clock", Status::clock_failed);
        return Status::clock_failed;
    }
    Status SampleTime(std::uint64_t& now) noexcept {
        goldcraft::effect_clock::Sample sample;
        if (const auto status = SampleTime(sample); status != Status::ok) return status;
        now = sample.milliseconds;
        return Status::ok;
    }

    Status Gate(const Context& context, std::uint64_t now) noexcept {
        if (clock_failed_) return Status::clock_failed;
        if (!active_) return Status::not_ready;
        if (context != context_) return Status::stale_context;
        if (!edicts || registry.world() != context_.world) {
            Failed("host world changed", Status::stale_context);
            return Status::not_ready;
        }
        // Reject a regressed caller timestamp before lifecycle revocation can
        // mutate the directory or shut the service down for an unrelated caller.
        if (const auto result = provider_.Pump(context_, now); result != Status::ok) return result;
        // API calls can occur between a ReAPI hook and the next host frame.
        // This read-only engine query is allocation-free and does not invoke
        // Touch/Use/AMXX callbacks. It only revokes; never invents a new life.
        for (unsigned slot = 1; slot < registered_.size(); ++slot) {
            const auto& target = registered_[slot];
            if (!target.world || Current(target)) continue;
            const auto result = provider_.InvalidateTarget(context_, target, now);
            if (result != Status::ok) {
                Failed("revalidate target", result);
                return result;
            }
            registered_[slot] = {};
            anchors_[slot] = {};
            spawned_[slot] = false;
        }
        RefreshAnchors(now);
        return Status::ok;
    }

public:
    explicit HostEffects(goldcraft::effect_clock::Reader reader = goldcraft::effect_clock::read_windows_qpc)
        : clock_(reader) {}

    void Begin(std::uint64_t world) noexcept {
        End();
        std::uint64_t now;
        if (SampleTime(now) != Status::ok) return;
        const auto result = provider_.BeginWorld(world, now);
        if (result != Status::ok) { Failed("begin world", result); return; }
        if (provider_.GetContext(&context_) != Status::ok) {
            Failed("read new context", Status::internal_error); return;
        }
        active_ = true;
    }

    void End() noexcept {
        const bool was_active = active_;
        active_ = false; // Before ReleaseControl can dispatch native Use callbacks.
        registered_ = {};
        anchors_ = {};
        spawned_ = {};
        connected_ = {};
        slot_epochs_ = {};
        transitions_ = {};
        if (!was_active) return;
        std::uint64_t now;
        if (SampleTime(now) != Status::ok) return;
        const auto result = provider_.EndWorld(context_, now);
        if (result != Status::ok) Failed("end world", result);
    }

    void Retire(unsigned slot) noexcept {
        if (!active_ || !slot || slot >= registered_.size() || !registered_[slot].world) return;
        std::uint64_t now;
        if (SampleTime(now) != Status::ok) return;
        const auto result = provider_.InvalidateTarget(context_, registered_[slot], now);
        if (result != Status::ok) { Failed("retire player", result); return; }
        registered_[slot] = {};
        anchors_[slot] = {};
        RefreshAnchors(now);
    }

    void Forget(unsigned slot) noexcept {
        if (!slot || slot >= spawned_.size()) return;
        // Killed runs before native deadflag changes. ReleaseControl may invoke
        // Pawn Use callbacks that switch forms: none may re-admit this dying life.
        spawned_[slot] = false;
        transitions_[slot].spawn_pending = false;
        Retire(slot);
    }

    void ResetSlot(unsigned slot) noexcept {
        if (!slot || slot >= registered_.size()) return;
        connected_[slot] = false;
        Forget(slot);
        transitions_[slot] = {};
        if (!active_) return;
        if (slot_epochs_[slot] == UINT64_MAX) {
            Failed("player transition epoch exhausted", Status::token_exhausted);
            return;
        }
        ++slot_epochs_[slot];
    }

    void Connected(unsigned slot) noexcept {
        ResetSlot(slot);
        if (!active_ || !slot || slot >= registered_.size()) return;
        const auto* binding = registry.get(slot);
        connected_[slot] = binding && binding->serial;
    }

    struct TransitionToken {
        Context context{};
        std::uint64_t slot_epoch = 0;
        std::uint32_t serial = 0;
    };

    TransitionToken StartTransition(unsigned slot, bool terminal) noexcept {
        if (!active_ || !slot || slot >= transitions_.size() || !connected_[slot]) return {};
        const auto* binding = registry.get(slot);
        if (!binding || !binding->serial) return {};
        const auto serial = binding->serial;
        auto& state = transitions_[slot];
        if (state.depth == UINT32_MAX) {
            Failed("player transition depth exhausted", Status::capacity_exhausted);
            return {};
        }
        ++state.depth;
        if (terminal) ++state.terminals;
        Forget(slot);
        return active_ ? TransitionToken{context_, slot_epochs_[slot], serial} : TransitionToken{};
    }

    void FinishTransition(const TransitionToken& token, unsigned slot, bool terminal, bool completed) noexcept {
        // An old stack frame can unwind after map teardown. It must not change
        // a new world's counters or admit a target in that new service context.
        if (!active_ || token.context != context_ || !slot || slot >= transitions_.size() ||
            !connected_[slot] || token.slot_epoch != slot_epochs_[slot]) return;
        const auto* binding = registry.get(slot);
        if (!binding || token.serial != binding->serial) return;
        auto& state = transitions_[slot];
        if (!state.depth || (terminal && !state.terminals)) {
            Failed("unbalanced player transition", Status::internal_error);
            return;
        }
        if (!completed) state.spawn_pending = false;
        if (terminal) { --state.terminals; state.spawn_pending = false; }
        if (--state.depth) return;
        const bool admit = state.spawn_pending && !state.terminals;
        state = {};
        if (admit) Spawn(slot);
    }

    void Spawn(unsigned slot) noexcept {
        if (!active_ || !slot || slot >= spawned_.size() || !connected_[slot]) return;
        Forget(slot);
        if (!active_) return;
        auto& state = transitions_[slot];
        // Respawning from a death/observer callback cannot reopen the effect
        // service before the outer terminal native function finishes.
        if (state.terminals) return;
        if (state.depth) { state.spawn_pending = true; return; }
        spawned_[slot] = true;
        Birth(slot);
    }

    void Birth(unsigned slot) noexcept {
        if (!active_ || !slot || slot >= registered_.size()) return;
        // A form/relocation callback within Spawn must not clear the completion
        // already recorded by the actual Spawn hook. Its outer guard will admit
        // the final real life once the native implementation has finished.
        if (transitions_[slot].depth) return;
        const auto* binding = registry.get(slot);
        if (!binding) { Retire(slot); return; }
        const Target target{registry.world(), Kind::player, slot, binding->serial, player_lives[slot]};
        if (!goldcraft::effects::valid_target(target) || !Current(target)) { Forget(slot); return; }
        if (registered_[slot] == target) return;
        Retire(slot);
        if (!active_) return;
        std::uint64_t now;
        if (SampleTime(now) != Status::ok) return;
        const auto result = provider_.RegisterTarget(context_, target, now);
        if (result != Status::ok) {
            RefreshAnchors(now); // RegisterTarget may have pumped other targets before rejecting.
            // Admission failure never retains effects from a previous life.
            // Do not fake a life increment or resurrect an invalidated identity.
            ALERT(at_console, "[GoldCraft] effect target admission slot=%u status=%u\n",
                  slot, static_cast<unsigned>(result));
            return;
        }
        registered_[slot] = target;
        RefreshAnchors(now);
    }

    void Frame() noexcept {
        if (!active_) return;
        std::uint64_t now;
        if (SampleTime(now) == Status::ok) (void)Gate(context_, now);
    }

    void ClockFault() noexcept override {
        if (clock_failed_) return;
        clock_failed_ = true;
        provider_.ClockFault();
        Failed("AMXX QPC clock", Status::clock_failed);
    }
    Status ObserveClock(const goldcraft::effect_clock::RawSample& sample) noexcept override {
        if (clock_failed_) return Status::clock_failed;
        goldcraft::effect_clock::Sample qualified;
        if (clock_.observe(sample, qualified) == goldcraft::effect_clock::Error::none &&
            provider_.ObserveClock(sample) == Status::ok) return Status::ok;
        ClockFault();
        return Status::clock_failed;
    }
    Status GetContext(Context* output) noexcept override {
        if (!output) return Status::invalid_state;
        std::uint64_t now;
        if (const auto result = SampleTime(now); result != Status::ok) return result;
        if (const auto result = Gate(context_, now); result != Status::ok) return result;
        *output = context_;
        return Status::ok;
    }
    Status CreateOwner(const Context& context, std::uint64_t now, std::uint64_t* output) noexcept override {
        if (const auto result = Gate(context, now); result != Status::ok) return result;
        return provider_.CreateOwner(context, now, output);
    }
    Status ResolveTarget(const Context& context, Kind kind, std::uint64_t key,
                         std::uint64_t now, Target* output) noexcept override {
        if (const auto result = Gate(context, now); result != Status::ok) return result;
        // MC objects need producer/UUID/life before they can enter this directory.
        if (kind != Kind::player) return Status::invalid_target;
        return provider_.ResolveTarget(context, kind, key, now, output);
    }
    Status Apply(const Context& context, std::uint64_t owner, const Target& target, const State& state,
                 std::uint32_t lease, std::uint64_t now, std::uint64_t* output) noexcept override {
        if (const auto result = Gate(context, now); result != Status::ok) return result;
        const auto result = provider_.Apply(context, owner, target, state, lease, now, output);
        RefreshAnchors(now); // Preserve registration result even if sampling fails.
        return result;
    }
    Status Update(const Context& context, std::uint64_t owner, std::uint64_t effect, const State& state,
                  std::uint32_t lease, std::uint64_t now) noexcept override {
        if (const auto result = Gate(context, now); result != Status::ok) return result;
        const auto result = provider_.Update(context, owner, effect, state, lease, now);
        RefreshAnchors(now); // Preserve registration result even if sampling fails.
        return result;
    }
    Status Remove(const Context& context, std::uint64_t owner, std::uint64_t effect, std::uint64_t now) noexcept override {
        if (const auto result = Gate(context, now); result != Status::ok) return result;
        const auto result = provider_.Remove(context, owner, effect, now);
        RefreshAnchors(now); // Preserve registration result even if sampling fails.
        return result;
    }
    Status RevokeOwner(const Context& context, std::uint64_t owner, std::uint64_t now) noexcept override {
        if (const auto result = Gate(context, now); result != Status::ok) return result;
        const auto result = provider_.RevokeOwner(context, owner, now);
        RefreshAnchors(now); // Preserve registration result even if sampling fails.
        return result;
    }
    // Native bridge only, not part of the public AMXX vtable. The transport
    // publisher still has to validate the actual pair/UUID/form and use the
    // recorded axis anchors. This method does not issue a remote execution grant.
    Status ReadLeasedSnapshot(unsigned slot, std::uint32_t freshnessMs,
                              goldcraft::amxx_effects::QualifiedLeasedSnapshot* output) noexcept {
        if (!output) return Status::invalid_state;
        goldcraft::effect_clock::Sample sample;
        if (const auto result = SampleTime(sample); result != Status::ok) return result;
        const auto now = sample.milliseconds;
        if (const auto result = Gate(context_, now); result != Status::ok) return result;
        if (!slot || slot >= registered_.size() || !registered_[slot].world) return Status::invalid_target;
        goldcraft::effects::LeasedSnapshot native;
        if (const auto result = provider_.ReadLeasedSnapshot(context_, registered_[slot], now, freshnessMs, &native);
            result != Status::ok) return result;
        return goldcraft::amxx_effects::qualify_lease(native, sample, *output) ? Status::ok : Status::invalid_lease;
    }
    // Bridge-private sample, not an AMXX ABI or a remote execution grant.
    // authorityConnection is the local Endpoint generation. It must never be
    // compared with the Java worker generation or used as an execution stream.
    struct ExecutionIdentity {
        Context context{};
        Target target{};
        std::array<std::uint8_t, 16> minecraftUuid{};
        std::uint64_t authorityConnection = 0;
        bool operator==(const ExecutionIdentity&) const = default;
    };
    struct ExecutionSample {
        ExecutionIdentity identity{};
        goldcraft::effects::Snapshot constraints{};
        std::uint64_t frequency = 0, issuedAtMs = 0, deadlineMs = 0, checkedAtMs = 0;
        std::array<float, 3> nativeFeet{}; // GoldSrc world units, not MC coordinates.
        std::array<float, 3> nativeAnchorFeet{}; // Held axes captured at their transitions.
        bool operator==(const ExecutionSample&) const = default;
    };

    // Inactive-control sampling only. This does not acquire movement, mark a
    // pose fresh, open an execution stream or admit a pose. The publisher must
    // still bind a placed/ACKed HostSession and revalidate at native admission.
    struct BootstrapSample {
        ExecutionIdentity identity{};
        goldcraft::effects::Snapshot constraints{};
        std::uint64_t frequency = 0, issuedAtMs = 0, deadlineMs = 0, checkedAtMs = 0;
        std::array<float, 3> nativeFeet{}; // GoldSrc world units, not MC coordinates.
        std::array<float, 3> nativeAnchorFeet{}; // Held axes captured at their transitions.
        bool operator==(const BootstrapSample&) const = default;
    };

private:
    enum class SamplePurpose { execution, bootstrap };
    template<class Sample>
    Status ReadAuthoritySample(const ExecutionIdentity& expected, std::uint32_t freshnessMs,
                               SamplePurpose purpose, Sample* output) noexcept {
        if (!output) return Status::invalid_state;
        goldcraft::effect_clock::Sample sampled;
        if (const auto result = SampleTime(sampled); result != Status::ok) return result;
        if (const auto result = Gate(expected.context, sampled.milliseconds); result != Status::ok) return result;
        const auto& target = expected.target;
        if (!goldcraft::effects::valid_target(target) || target.kind != Kind::player ||
            target.world != expected.context.world || target.key >= registered_.size() ||
            expected.minecraftUuid == std::array<std::uint8_t, 16>{}) return Status::invalid_target;
        const auto slot = static_cast<unsigned>(target.key);
        if (registered_[slot] != target || !Current(target)) return Status::invalid_target;
        const auto matchingPair = [&]() noexcept {
            const auto* pair = registry.get(slot);
            return pair && pair->paired && pair->serial == target.serial &&
                pair->uuid == expected.minecraftUuid && minecraft_forms[slot];
        };
        const auto activeConnection = [&]() noexcept {
            return expected.authorityConnection && link.connected() &&
                expected.authorityConnection == connection_generation &&
                expected.authorityConnection == link.generation();
        };
        const auto matchingControl = [&]() noexcept {
            if (!gpGlobals || !std::isfinite(gpGlobals->time)) return false;
            if (purpose == SamplePurpose::bootstrap) return !controlled[slot].active;
            if (!controlled[slot].active ||
                !std::isfinite(controlled[slot].last_update)) return false;
            const auto age = gpGlobals->time - controlled[slot].last_update;
            return age >= 0.0f && age <= 1.0f;
        };
        if (!activeConnection()) return Status::stale_context;
        if (!matchingPair() || !matchingControl()) return Status::not_ready;

        goldcraft::effects::LeasedSnapshot leased;
        if (const auto result = provider_.ReadLeasedSnapshot(expected.context, target,
                sampled.milliseconds, freshnessMs, &leased); result != Status::ok) return result;
        goldcraft::amxx_effects::QualifiedLeasedSnapshot qualified;
        if (!goldcraft::amxx_effects::qualify_lease(leased, sampled, qualified)) return Status::invalid_lease;
        // Match LocalQpcLease's positive signed range. Subtraction before the
        // addition avoids both unsigned wrap and an unrepresentable Java value.
        constexpr auto javaMax = static_cast<std::uint64_t>(INT64_MAX);
        if (sampled.milliseconds > javaMax || qualified.safeRemainingMs > javaMax - sampled.milliseconds)
            return Status::invalid_lease;
        const auto deadline = sampled.milliseconds + qualified.safeRemainingMs;
        const auto& vars = edicts[slot].v;
        const std::array<float, 3> feet{vars.origin.x, vars.origin.y, vars.origin.z + vars.mins.z};
        for (const auto coordinate : feet) if (!std::isfinite(coordinate)) return Status::invalid_state;

        // Check again after assembling the sample. A thread suspension must not
        // restart the lease at return time. This clock read is local; never feed
        // a peer's historical sample into ObserveClock's raw high-water mark.
        goldcraft::effect_clock::Sample checked;
        if (const auto result = SampleTime(checked); result != Status::ok) return result;
        if (checked.frequency != sampled.frequency || checked.milliseconds >= deadline) return Status::invalid_lease;
        if (const auto result = Gate(expected.context, checked.milliseconds); result != Status::ok) return result;
        if (registered_[slot] != target || !Current(target)) return Status::invalid_target;
        if (!activeConnection()) return Status::stale_context;
        if (!matchingPair() || !matchingControl()) return Status::not_ready;
        const auto& record = anchors_[slot];
        const auto held = leased.snapshot.movement.holdAxes;
        if (record.fault || record.target != target || record.snapshot != leased.snapshot ||
            (record.validAxes & held) != held) return Status::invalid_state;
        auto anchor = feet;
        for (unsigned axis = 0; axis < anchor.size(); ++axis)
            if (held & (std::uint32_t{1} << axis)) anchor[axis] = record.feet[axis];
        *output = {expected, leased.snapshot, sampled.frequency, sampled.milliseconds,
                   deadline, checked.milliseconds, feet, anchor};
        return Status::ok;
    }

public:
    Status ReadExecutionSample(const ExecutionIdentity& expected, std::uint32_t freshnessMs,
                               ExecutionSample* output) noexcept {
        return ReadAuthoritySample(expected, freshnessMs, SamplePurpose::execution, output);
    }
    Status ReadBootstrapSample(const ExecutionIdentity& expected, std::uint32_t freshnessMs,
                               BootstrapSample* output) noexcept {
        return ReadAuthoritySample(expected, freshnessMs, SamplePurpose::bootstrap, output);
    }

    Status ReadSnapshot(unsigned slot, goldcraft::effects::Snapshot* output) noexcept {
        std::uint64_t now;
        if (const auto result = SampleTime(now); result != Status::ok) return result;
        if (const auto result = Gate(context_, now); result != Status::ok) return result;
        if (!slot || slot >= registered_.size() || !registered_[slot].world) return Status::invalid_target;
        return provider_.ReadSnapshot(context_, registered_[slot], now, output);
    }
};

HostEffects host_effects;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(HostEffects, IEffectService,
    goldcraft::amxx_effects::service_version, host_effects);
