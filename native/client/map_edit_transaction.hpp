#pragma once
#include "map_collision.hpp"
#include <IMetaRendererWorldEdit.h>

namespace goldcraft::client_map {
// The received Replica can advance while GPU leaves are preparing. Only the
// last successfully committed revision supplies prediction/rendering together.
class EditTransaction {
  public:
    void reset(std::uint64_t epoch);
    edits::Applied accept(const edits::Snapshot &snapshot);
    edits::Applied accept(const edits::Delta &delta);
    void pump(IMetaRendererWorldEdit *renderer, model_s *world);
    void invalidate();
    bool ready() const { return desired_.ready(); }
    bool pending() const { return dirty_ || ticket_ != 0; }
    const edits::Snapshot &state() const { return desired_.state(); }
    const edits::Snapshot &applied() const { return applied_.state(); }

  private:
    void cancel();
    edits::Applied receive(edits::Replica next, edits::Applied result);
    edits::Replica desired_, applied_, prepared_;
    Candidate physics_;
    IMetaRendererWorldEdit *renderer_ = nullptr;
    model_s *loaded_ = nullptr;
    std::uint64_t ticket_ = 0;
    bool dirty_ = false;
};
} // namespace goldcraft::client_map
