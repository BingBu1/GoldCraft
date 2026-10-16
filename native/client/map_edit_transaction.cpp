#include "map_edit_transaction.hpp"
#include <type_traits>
#include <utility>

namespace goldcraft::client_map {
void EditTransaction::cancel() {
    EditWorkScope work(work_history_, EditStage::cancel, work_identity_);
    if (renderer_ && ticket_)
        renderer_->Cancel(ticket_);
    ticket_ = 0;
    physics_task_.reset();
    physics_.reset();
}
void EditTransaction::reset(std::uint64_t epoch) {
    cancel();
    if (renderer_)
        renderer_->Reset();
    renderer_ = nullptr;
    targeted_ = nullptr;
    loaded_ = nullptr;
    publication_ = 0;
    published_ = false;
    dirty_ = false;
    desired_.reset(epoch);
    applied_.reset(epoch);
    prepared_.reset(epoch);
    client_map::brush_identities().reset(epoch);
    client_map::reset();
}
void EditTransaction::invalidate() {
    cancel();
    dirty_ = false;
    desired_.invalidate();
}
edits::Applied EditTransaction::receive(edits::Replica next, edits::Applied result) {
    if (result != edits::Applied::ignored) {
        // Continuous additive explosions must not starve an in-flight large
        // preparation. Its exact revision may commit before the newest one.
        // Clears/removals, gaps and identity changes cancel immediately.
        const auto& before=desired_.state();const auto& after=next.state();
        const bool extends=ticket_&&prepared_.state().cuts.size()>=32&&result==edits::Applied::changed&&
            after.epoch==before.epoch&&after.cuts.size()>=before.cuts.size()&&
            std::equal(before.cuts.begin(),before.cuts.end(),after.cuts.begin());
        if(!extends)cancel();
        desired_ = std::move(next);
        dirty_ = result == edits::Applied::changed;
    }
    return result;
}
edits::Applied EditTransaction::accept(const edits::Snapshot &snapshot) {
    auto next = desired_;
    const auto result = next.accept(snapshot);
    return receive(std::move(next), result);
}
edits::Applied EditTransaction::accept(const edits::Delta &delta) {
    auto next = desired_;
    const auto result = next.accept(delta);
    return receive(std::move(next), result);
}
void EditTransaction::provider_lost() {
    const auto epoch = desired_.state().epoch;
    cancel();
    if (renderer_)
        renderer_->Reset();
    renderer_ = nullptr;
    targeted_ = nullptr;
    loaded_ = nullptr;
    publication_ = 0;
    published_ = dirty_ = false;
    desired_.invalidate();
    applied_.reset(epoch);
    prepared_.reset(epoch);
    client_map::brush_identities().invalidate();
    client_map::reset();
}
void EditTransaction::publish(IMetaRendererWorldEdit *renderer, model_s *world,
                              IMetaRendererWorldEdit2 *targeted) {
    // Publication can precede a new preparation. Do not label it with the
    // previous candidate's ticket/hash when the received ledger has advanced.
    const EditWorkIdentity identity{0, desired_.state().epoch, desired_.state().revision,
        0, 0, static_cast<std::uint32_t>(desired_.state().cuts.size())};
    EditWorkScope work(work_history_, EditStage::identity, identity);
    if (targeted && static_cast<IMetaRendererWorldEdit *>(targeted) != renderer) {
        provider_lost();
        throw ProtocolError("Client world edit interface mismatch");
    }
    if (renderer_ && (renderer_ != renderer || targeted_ != targeted ||
                      (loaded_ && world != loaded_))) {
        // A world/provider replacement requires an authenticated new binding.
        // Revoke both consumers before attempting any replacement preparation.
        provider_lost();
        throw ProtocolError("Client world edit provider/model changed");
    }
    if (!renderer)
        return;
    renderer_ = renderer;
    targeted_ = targeted;
    loaded_ = world;
    if (!targeted_)
        return;
    auto &receiver = client_map::brush_identities();
    const auto generation = receiver.publication();
    if (published_ && generation == publication_)
        return;
    const auto *frame = receiver.frame();
    if (!frame) {
        // Revocation deliberately returns false in API002; it is not a failed
        // publication and must not force a static-world snapshot retry.
        targeted_->UpdateBrushSnapshot(0, 0, 0, nullptr, 0);
    } else {
        // These records have different C++ types. Copy into fixed owned storage;
        // never reinterpret the receiver frame even when their layouts match.
        for (std::size_t i = 0; i < frame->total; ++i) {
            const auto &entry = frame->entries[i];
            identities_[i] = {entry.slot, entry.serial, entry.model};
        }
        if (!targeted_->UpdateBrushSnapshot(frame->epoch, frame->revision, frame->sequence,
                                           identities_.data(), frame->total)) {
            receiver.invalidate();
            targeted_->UpdateBrushSnapshot(0, 0, 0, nullptr, 0);
            publication_ = 0;
            published_ = true;
            invalidate();
            throw ProtocolError("Renderer rejected brush identity snapshot");
        }
    }
    publication_ = generation;
    published_ = true;
}
void EditTransaction::pump(IMetaRendererWorldEdit *renderer, model_s *world,
                           IMetaRendererWorldEdit2 *targeted) {
    publish(renderer, world, targeted);
    if (!desired_.ready() || !desired_.state().epoch || !renderer || !world)
        return;
    if (dirty_ && !ticket_) {
        try {
            std::vector<MetaWorldEditBox> boxes;
            std::vector<MetaWorldEditCut> cuts;
            {
                EditWorkScope work(work_history_, EditStage::copy, work_identity_);
                work_identity_ = edit_work_identity(desired_.state(), ++attempt_);
                prepared_ = desired_;
                if (targeted_)
                    cuts.reserve(prepared_.state().cuts.size());
                else
                    boxes.reserve(prepared_.state().cuts.size());
                for (const auto &cut : prepared_.state().cuts) {
                    if (cut.target.slot && !targeted_)
                        throw ProtocolError("Dynamic BSP edits require per-instance Renderer support");
                    MetaWorldEditBox box{};
                    for (int axis = 0; axis < 3; ++axis) {
                        box.min[axis] = cut.box.min[axis];
                        box.max[axis] = cut.box.max[axis];
                    }
                    if (targeted_)
                        cuts.push_back({{cut.target.slot, cut.target.serial, cut.target.model}, box});
                    else
                        boxes.push_back(box);
                }
            }
            // Mining one block keeps the short synchronous path. An explosion
            // builds from owned hull snapshots off the render/input thread.
            {
                EditWorkScope work(work_history_, EditStage::physics_prepare, work_identity_);
                if (prepared_.state().cuts.size() >= 32)
                    physics_task_ = client_map::prepare_async(prepared_, world);
                else
                    physics_ = client_map::prepare(prepared_, world);
            }
            {
                EditWorkScope work(work_history_, EditStage::renderer_begin, work_identity_);
                ticket_ = targeted_
                    ? targeted_->BeginTargeted(world, prepared_.state().epoch, prepared_.state().revision,
                                               cuts.data(), static_cast<uint32_t>(cuts.size()))
                    : renderer_->Begin(world, prepared_.state().epoch, prepared_.state().revision,
                                       boxes.data(), static_cast<uint32_t>(boxes.size()));
                work_identity_.ticket = ticket_;
                if (!ticket_)
                    throw ProtocolError("Renderer refused world edit preparation");
            }
            dirty_ = false;
        } catch (...) {
            invalidate();
            throw;
        }
    }
    if (!ticket_)
        return;
    if (physics_task_) {
        try {
            EditWorkScope work(work_history_, EditStage::physics_poll, work_identity_);
            if (!client_map::poll(physics_task_, physics_)) return;
            physics_task_.reset();
        } catch (...) {
            invalidate();
            throw;
        }
    }
    MetaWorldEditStatus status;
    {
        EditWorkScope work(work_history_, EditStage::renderer_poll, work_identity_);
        status = renderer_->Poll(ticket_);
        if (status != MetaWorldEditStatus::Preparing && status != MetaWorldEditStatus::Ready)
            work.fail();
    }
    if (status == MetaWorldEditStatus::Preparing)
        return;
    if (status != MetaWorldEditStatus::Ready) {
        invalidate();
        throw ProtocolError("Renderer world edit preparation failed or expired");
    }
    // No callback/yield between renderer publication and these noexcept moves.
    // A failed render commit leaves both collision and applied Replica intact.
    {
        EditWorkScope work(work_history_, EditStage::renderer_commit, work_identity_);
        if (!renderer_->Commit(ticket_)) return;
    }
    ticket_ = 0;
    EditWorkScope work(work_history_, EditStage::collision_commit, work_identity_);
    client_map::commit(std::move(physics_));
    static_assert(std::is_nothrow_move_assignable_v<edits::Replica>);
    applied_ = std::move(prepared_);
}
} // namespace goldcraft::client_map
