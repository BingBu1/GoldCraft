// Hold a real collision worker at an allocation, so this does not depend on
// machine speed or sleeps. Renderer progress/failure must not wait for it.
void parallel_preparation_progress() {
    struct Renderer final : IMetaRendererWorldEdit {
        std::uint64_t ticket{}, revision{}, visible{};
        unsigned polls{}, commits{}, cancels{};
        MetaWorldEditStatus status = MetaWorldEditStatus::Ready;
        uint64_t Begin(model_s*, uint64_t, uint64_t value,
                       const MetaWorldEditBox*, uint32_t) noexcept override {
            revision = value; return ticket = 1;
        }
        MetaWorldEditStatus Poll(uint64_t value) noexcept override {
            assert(value == ticket && value); ++polls; return status;
        }
        bool Commit(uint64_t value) noexcept override {
            assert(value == ticket && value); ++commits; visible = revision; ticket = 0; return true;
        }
        void Cancel(uint64_t value) noexcept override {
            assert(value == ticket && value); ++cancels; ticket = 0;
        }
        void Reset() noexcept override { ticket = visible = 0; }
    } renderer;
    struct WorkerGate {
        WorkerGate() { worker_allocation_held = false; hold_worker_allocation = true; }
        void release() { hold_worker_allocation = false; hold_worker_allocation.notify_all(); }
        ~WorkerGate() { release(); }
        void wait() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!worker_allocation_held.load()) {
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("Collision worker did not reach allocation gate");
                std::this_thread::yield();
            }
        }
    };
    client_map::shutdown_preparation();
    setup();
    edits::Snapshot batch{9, 33, {}};
    for (std::uint64_t id = 2; id <= 33; ++id)
        batch.cuts.push_back({id, {}, {{-1, float(int(id)-18)*4, -64}, {17, float(int(id)-17)*4, 64}}});
    // Keep the large identity/diagnostic arrays off this fixture's call stack.
    auto owner = std::make_unique<client_map::EditTransaction>();
    auto& transaction = *owner;
    transaction.reset(9);
    transaction.accept(batch);
    {
        WorkerGate gate;
        transaction.pump(&renderer, &model);
        gate.wait();
        const auto polls = renderer.polls;
        transaction.pump(&renderer, &model);
        assert(renderer.polls == polls + 1 && !renderer.commits);
        assert(transaction.applied().revision == 0 && renderer.visible == 0);
        float a[]{-64, 0, 0}, b[]{64, 0, 0};
        run(a, b); assert(result.hitgroup == 77);
        gate.release();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (transaction.pending()) {
            transaction.pump(&renderer, &model);
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        assert(renderer.commits == 1 && renderer.visible == 33 && transaction.applied().revision == 33);
        run(a, b); assert(result.fraction == 1);
    }
    transaction.reset(9);
    transaction.accept(batch);
    {
        WorkerGate gate;
        renderer.status = MetaWorldEditStatus::Preparing;
        transaction.pump(&renderer, &model);
        gate.wait();
        renderer.status = MetaWorldEditStatus::Failed;
        bool rejected = false;
        try { transaction.pump(&renderer, &model); }
        catch (const ProtocolError&) { rejected = true; }
        assert(rejected && !transaction.ready() && renderer.cancels == 1 && renderer.commits == 1);
        assert(transaction.applied().revision == 0 && renderer.visible == 0);
    }
    transaction.reset(0);
    client_map::shutdown_preparation();
    std::cout << "Parallel preparation: Renderer polls during held collision, no early commit, exact paired commit, immediate failure cancellation passed\n";
}

// A Renderer needs several frames to prepare even a small edited world. Feed
// authoritative additions faster than that and exercise the real transaction.
void additive_progress() {
    struct Renderer final : IMetaRendererWorldEdit {
        std::uint64_t next{}, ticket{}, revision{}, visible{};
        unsigned begins{}, cancels{}, commits{}, polls{};
        bool commit_ok = true;
        uint64_t Begin(model_s*, uint64_t, uint64_t value,
                       const MetaWorldEditBox*, uint32_t) noexcept override {
            ++begins; polls = 0; revision = value; return ticket = ++next;
        }
        MetaWorldEditStatus Poll(uint64_t value) noexcept override {
            assert(value && value == ticket);
            return ++polls >= 3 ? MetaWorldEditStatus::Ready : MetaWorldEditStatus::Preparing;
        }
        bool Commit(uint64_t value) noexcept override {
            assert(value && value == ticket);
            if (!commit_ok) return false;
            visible = revision; ticket = 0; ++commits; return true;
        }
        void Cancel(uint64_t value) noexcept override {
            assert(value && value == ticket); ticket = 0; ++cancels;
        }
        void Reset() noexcept override { ticket = visible = 0; }
    } renderer;
    auto require = [](bool value, const char* what) {
        if (!value) throw std::runtime_error(what);
    };
    setup();
    client_map::EditTransaction transaction;
    transaction.reset(9);
    edits::Snapshot latest{9, 1, {}};
    for (std::uint64_t revision = 2; revision <= 32; ++revision) {
        const float y = float(revision - 2) * 160;
        const edits::Cut cut{revision, {}, {{-1, y - 64, -64}, {17, y + 64, 64}}};
        latest.revision = revision; latest.cuts.push_back(cut);
        // Recovery snapshots that extend the same prefix must progress too.
        const auto accepted = revision % 2 ? transaction.accept(latest) :
            revision == 2 ? transaction.accept(latest) :
            transaction.accept(edits::Delta{9, revision - 1, revision,
                                           edits::Operation::add, {}, cut.box});
        require(accepted == edits::Applied::changed, "Additive corpus was rejected");
        transaction.pump(&renderer, &model);
        require(renderer.visible == transaction.applied().revision, "Visual/collision revision diverged");
        if (revision >= 4)
            require(transaction.applied().revision >= revision - 4,
                    "Small additive stream starved the prepared revision");
    }
    const auto commits_during_stream = renderer.commits;
    require(commits_during_stream >= 10 && renderer.cancels == 0,
            "Additions cancelled useful prepared work");
    for (int frame = 0; frame < 6 && transaction.pending(); ++frame)
        transaction.pump(&renderer, &model);
    require(!transaction.pending() && transaction.applied().cuts == latest.cuts &&
            transaction.applied().revision == latest.revision && renderer.visible == latest.revision,
            "Small additive stream failed to reach the exact final state");
    float from[]{-64, 0, 0}, to[]{64, 0, 0};
    run(from, to); require(result.fraction == 1, "Committed opening has stale collision");

    // Ready-but-not-committable work is retained; a clear must still revoke it
    // immediately and restore the original surface/collision together.
    latest.revision = 33;
    latest.cuts.push_back({33, {}, {{-1, 5000, -64}, {17, 5100, 64}}});
    transaction.accept(latest); transaction.pump(&renderer, &model);
    const auto inflight = renderer.ticket;
    renderer.commit_ok = false;
    latest.revision = 34;
    latest.cuts.push_back({34, {}, {{-1, 5200, -64}, {17, 5300, 64}}});
    transaction.accept(latest);
    require(renderer.ticket == inflight, "Ready work was cancelled by an addition");
    for (int frame = 0; frame < 3; ++frame) transaction.pump(&renderer, &model);
    require(transaction.applied().revision == 32, "Uncommitted work changed collision");
    const auto before_clear = renderer.cancels;
    transaction.accept(edits::Delta{9, 34, 35, edits::Operation::clear, {}, {}});
    require(renderer.ticket == 0 && renderer.cancels == before_clear + 1,
            "Round clear failed to revoke in-flight additions");
    renderer.commit_ok = true;
    for (int frame = 0; frame < 3; ++frame) transaction.pump(&renderer, &model);
    require(!transaction.pending() && renderer.visible == 35 && transaction.applied().cuts.empty(),
            "Round clear failed to commit");
    run(from, to); require(result.hitgroup == 77, "Cleared world retained old hole collision");
    transaction.reset(0);
    std::cout << "Small additive stream: 31 cuts, " << commits_during_stream
              << " intermediate commits, zero restart cancellations; exact final state and clear passed\n";
}
