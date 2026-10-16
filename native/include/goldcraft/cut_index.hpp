#pragma once
#include "goldcraft/world_carving.hpp"
#include <algorithm>
#include <bit>
#include <numeric>

namespace goldcraft::carving {
// Broad phase only. Inclusive overlap retains touching faces and zero-width
// ray bounds. Results keep source order so clipping and tie-breaking are exact.
// The caller validates boxes and accounts all build/query work in its budget.
class CutIndex {
    struct Node { Box bounds; std::size_t first, last, left = 0, right = 0; };
    std::span<const Box> boxes_;
    std::vector<std::size_t> order_;
    std::vector<Node> nodes_;
    static bool overlaps(const Box& a, const Box& b) {
        for (int axis=0; axis<3; ++axis)
            if (a.max[axis] < b.min[axis] || b.max[axis] < a.min[axis]) return false;
        return true;
    }
    template<class Consume> std::size_t build(std::size_t first, std::size_t last, Consume& consume) {
        consume(last-first);
        Box bounds=boxes_[order_[first]];
        for (auto i=first+1; i<last; ++i) for (int axis=0; axis<3; ++axis) {
            bounds.min[axis]=std::min(bounds.min[axis],boxes_[order_[i]].min[axis]);
            bounds.max[axis]=std::max(bounds.max[axis],boxes_[order_[i]].max[axis]);
        }
        const auto node=nodes_.size(); nodes_.push_back({bounds,first,last});
        if (last-first>8) {
            int axis=0;
            for (int a=1; a<3; ++a)
                if (bounds.max[a]-bounds.min[a]>bounds.max[axis]-bounds.min[axis]) axis=a;
            const auto middle=first+(last-first)/2;
            consume((last-first)*std::bit_width(last-first));
            std::nth_element(order_.begin()+first,order_.begin()+middle,order_.begin()+last,[&](auto a,auto b) {
                const auto x=boxes_[a].min[axis]+boxes_[a].max[axis];
                const auto y=boxes_[b].min[axis]+boxes_[b].max[axis];
                return x!=y ? x<y : a<b;
            });
            const auto left=build(first,middle,consume),right=build(middle,last,consume);
            nodes_[node].left=left; nodes_[node].right=right;
        }
        return node;
    }
    template<class Consume> void visit(std::size_t at,const Box& bounds,
                                      std::vector<std::size_t>& out,Consume& consume) const {
        consume(1);
        const auto& node=nodes_[at];
        if (!overlaps(node.bounds,bounds)) return;
        if (node.right) { visit(node.left,bounds,out,consume); visit(node.right,bounds,out,consume); }
        else {
            consume(node.last-node.first);
            for(auto i=node.first;i<node.last;++i)
                if(overlaps(boxes_[order_[i]],bounds)) out.push_back(order_[i]);
        }
    }
  public:
    template<class Consume> CutIndex(std::span<const Box> boxes,Consume consume):boxes_(boxes) {
        consume(boxes.size());
        order_.resize(boxes.size()); std::iota(order_.begin(),order_.end(),std::size_t{0});
        if(!boxes.empty()) build(0,boxes.size(),consume);
    }
    template<class Consume> void query(const Box& bounds,std::vector<std::size_t>& out,Consume consume) const {
        out.clear();
        if(!nodes_.empty()) visit(0,bounds,out,consume);
        consume(out.size()*std::bit_width(out.size()));
        std::sort(out.begin(),out.end());
    }
};
}
