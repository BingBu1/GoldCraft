#include "map_edit_transaction.hpp"
#include <type_traits>
#include <utility>

namespace goldcraft::client_map {
void EditTransaction::cancel() {
    if (renderer_ && ticket_)
        renderer_->Cancel(ticket_);
    ticket_ = 0;
    physics_.reset();
}
void EditTransaction::reset(std::uint64_t epoch) {
    cancel();
    if (renderer_)
        renderer_->Reset();
    renderer_ = nullptr;
    loaded_ = nullptr;
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
        cancel();
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
void EditTransaction::pump(IMetaRendererWorldEdit *renderer, model_s *world) {
    if (!desired_.ready() || !desired_.state().epoch)
        return;
    if (!renderer || !world)
        return;
    if (renderer_ && (renderer_ != renderer || (loaded_ && world != loaded_))) {
        // A world/provider replacement requires an authenticated new binding.
        invalidate();
        throw ProtocolError("Client world edit provider/model changed");
    }
    renderer_ = renderer;
    loaded_ = world;
    if (dirty_) {
        try {
            prepared_ = desired_;
            std::vector<MetaWorldEditBox> boxes;
            boxes.reserve(prepared_.state().cuts.size());
            for (const auto &cut : prepared_.state().cuts) {
                if (cut.target.slot)
                    throw ProtocolError("Dynamic BSP edits require verified client entity serials");
                MetaWorldEditBox box{};
                for (int axis = 0; axis < 3; ++axis) {
                    box.min[axis] = cut.box.min[axis];
                    box.max[axis] = cut.box.max[axis];
                }
                boxes.push_back(box);
            }
            physics_ = client_map::prepare(prepared_, world);
            ticket_ = renderer_->Begin(world, prepared_.state().epoch, prepared_.state().revision,
                                       boxes.data(), static_cast<uint32_t>(boxes.size()));
            if (!ticket_)
                throw ProtocolError("Renderer refused world edit preparation");
            dirty_ = false;
        } catch (...) {
            invalidate();
            throw;
        }
    }
    if (!ticket_)
        return;
    const auto status = renderer_->Poll(ticket_);
    if (status == MetaWorldEditStatus::Preparing)
        return;
    if (status != MetaWorldEditStatus::Ready) {
        invalidate();
        throw ProtocolError("Renderer world edit preparation failed or expired");
    }
    // No callback/yield between renderer publication and these noexcept moves.
    // A failed render commit leaves both collision and applied Replica intact.
    if (!renderer_->Commit(ticket_))
        return;
    ticket_ = 0;
    client_map::commit(std::move(physics_));
    static_assert(std::is_nothrow_move_assignable_v<edits::Replica>);
    applied_ = std::move(prepared_);
}
} // namespace goldcraft::client_map
