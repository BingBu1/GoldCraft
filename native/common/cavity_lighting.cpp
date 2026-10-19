#include "goldcraft/cavity_lighting.hpp"
#include "goldcraft/cut_index.hpp"
#include "goldcraft/cut_union.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace goldcraft::lighting {
class SampleCache {
  public:
    struct Entry {
        std::array<std::uint32_t,6> key;
        surface::Range values;
        std::uint32_t rays{}, occluded{};
    };
    std::uint32_t first_face{}, face_count{};
    double radius{};
    std::size_t neighbours{};
    std::int32_t root{};
    std::vector<carving::Plane> planes;
    std::vector<carving::HullNode> nodes;
    std::vector<carving::Box> cuts;
    std::vector<Probe> probes;
    std::vector<Value> probe_values, values;
    std::vector<Entry> entries;
};
std::uint64_t cache_bytes(const SampleCache& cache) noexcept {
    return sizeof(cache) + cache.planes.capacity()*sizeof(carving::Plane) +
        cache.nodes.capacity()*sizeof(carving::HullNode) + cache.cuts.capacity()*sizeof(carving::Box) +
        cache.probes.capacity()*sizeof(Probe) + (cache.probe_values.capacity()+cache.values.capacity())*sizeof(Value) +
        cache.entries.capacity()*sizeof(SampleCache::Entry);
}
namespace {
using carving::Point;
Point sub(const Point &a, const Point &b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
double dot(const Point &a, const Point &b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Point point(const float (&p)[3]) { return {p[0], p[1], p[2]}; }
void valid(const Point &p) {
    for (double v : p) if (!std::isfinite(v) || std::abs(v) > 1'000'000)
        throw std::invalid_argument("Cavity lighting coordinate");
}
void consume(std::size_t &work, std::size_t limit, std::size_t n = 1) {
    if (work > limit || n > limit-work) throw std::length_error("Cavity lighting work budget");
    work += n;
}
struct Interval { double begin, end; };
struct Visibility {
    carving::HullView hull;
    Point start, delta;
    std::vector<Interval> &openings;
    std::size_t &work;
    std::size_t limit;
    bool covered(double a, double b) const {
        const auto it = std::lower_bound(openings.begin(), openings.end(), a,
            [](const Interval &part, double value) { return part.end < value; });
        return it != openings.end() && it->begin <= a && it->end >= b;
    }
    bool visit(std::int32_t node, double a, double b, unsigned depth = 0) {
        consume(work, limit);
        if (a >= b || covered(a, b)) return true;
        if (node < 0) return node != -2;
        if (depth >= 256 || std::size_t(node) >= hull.nodes.size())
            throw std::invalid_argument("Cavity lighting BSP node");
        const auto &branch = hull.nodes[node];
        if (branch.plane >= hull.planes.size()) throw std::invalid_argument("Cavity lighting BSP plane");
        const auto &plane = hull.planes[branch.plane];
        const double distance = dot(start, plane.normal)-plane.distance, speed = dot(delta, plane.normal);
        const double da = distance+speed*a, db = distance+speed*b;
        if (da >= 0 && db >= 0) return visit(branch.children[0], a, b, depth+1);
        if (da <= 0 && db <= 0) return visit(branch.children[1], a, b, depth+1);
        const double split = std::clamp(-distance/speed, a, b);
        const unsigned near = speed > 0 ? 1 : 0;
        return visit(branch.children[near], a, split, depth+1) && visit(branch.children[near^1], split, b, depth+1);
    }
};
bool visible_with_scratch(carving::HullView hull, std::span<const carving::Box> cuts, Point start, Point end,
                          std::size_t &operations, std::size_t limit, std::vector<Interval> &openings) {
    valid(start); valid(end);
    openings.clear();
    Visibility view{hull, start, sub(end, start), openings, operations, limit};
    consume(operations, limit, cuts.size());
    for (const auto &cut : cuts) {
        Interval part{0, 1};
        for (int axis = 0; axis < 3; ++axis) {
            if (view.delta[axis] == 0) {
                if (start[axis] < cut.min[axis] || start[axis] > cut.max[axis]) { part.end = -1; break; }
            } else {
                const auto a = (cut.min[axis]-start[axis])/view.delta[axis];
                const auto b = (cut.max[axis]-start[axis])/view.delta[axis];
                part.begin = std::max(part.begin, std::min(a,b));
                part.end = std::min(part.end, std::max(a,b));
            }
        }
        if (part.begin < part.end) view.openings.push_back(part);
    }
    consume(operations, limit, view.openings.size()*std::max<std::size_t>(1, std::bit_width(view.openings.size())));
    std::sort(view.openings.begin(), view.openings.end(), [](auto a, auto b) { return a.begin < b.begin; });
    std::size_t kept = 0;
    for (auto part : view.openings) {
        if (kept && view.openings[kept-1].end >= part.begin)
            view.openings[kept-1].end = std::max(view.openings[kept-1].end, part.end);
        else view.openings[kept++] = part;
    }
    view.openings.resize(kept);
    return view.visit(hull.root, 0, 1);
}

struct Candidate {
    double distance;
    std::uint32_t probe;
    bool operator<(const Candidate &other) const {
        return distance != other.distance ? distance < other.distance : probe < other.probe;
    }
};
// Median tree over immutable map probes; a query only visits nearby partitions.
// The heap is reused for every sample and never appears in the drawing path.
class Field {
  public:
    Field(const Model &model, const Settings &settings, std::size_t &work, const std::atomic_bool* cancelled)
        : model_(model), settings_(settings), work_(work), cancelled_(cancelled), order_(model.probes.size()), bounds_(model.probes.size()),
          visibility_cuts_(carving::compact_cuts(model.cuts,[&](auto n){consume(work,settings.mesh.operations,n);})),
          cut_index_(visibility_cuts_,[&](auto n){consume(work,settings.mesh.operations,n);}) {
        if (order_.empty()) throw std::invalid_argument("Missing baked map lighting probes");
        if (order_.size() > settings.probes || model.values.size() > settings.values)
            throw std::length_error("Cavity lighting probe capacity");
        for (const auto &value : model.values) {
            consume(work_, settings_.mesh.operations);
            if (value.style >= 255) throw std::invalid_argument("Cavity lighting style");
            for (float c : value.rgb) if (!std::isfinite(c) || c < 0 || c > 1)
                throw std::invalid_argument("Cavity lighting coefficient");
        }
        for (const auto &probe : model.probes) {
            consume(work_, settings_.mesh.operations);
            valid(probe.position);
            if (!probe.values.count || probe.values.first > model.values.size() ||
                probe.values.count > model.values.size()-probe.values.first)
                throw std::invalid_argument("Cavity lighting probe range");
            std::array<bool, 255> styles{};
            for (std::size_t i = probe.values.first; i < std::size_t(probe.values.first)+probe.values.count; ++i) {
                consume(work_, settings_.mesh.operations);
                if (styles[model.values[i].style]) throw std::invalid_argument("Duplicate probe light style");
                styles[model.values[i].style] = true;
            }
        }
        std::iota(order_.begin(), order_.end(), 0u);
        split(0, order_.size(), 0);
        heap_.reserve(settings.neighbours);
        if(model.cuts.size()>=32)ray_cache_.resize(131072);
    }
    surface::Range sample(Point p, Point normal, Prepared &result) {
        if(cancelled_ && cancelled_->load(std::memory_order_relaxed))throw std::runtime_error("Cavity lighting cancelled");
        valid(p); valid(normal);
        if (std::abs(dot(normal, normal)-1) > 1e-4) throw std::invalid_argument("Cavity lighting normal");
        heap_.clear();
        query(0, order_.size(), 0, p, normal);
        // Stable summation, independent of the median partition/heap order.
        std::sort(heap_.begin(), heap_.end());
        const auto hull = local_hull(p);
        std::array<Point, 255> coefficients{};
        double total = 0;
        for (const auto &candidate : heap_) {
            consume(work_, settings_.mesh.operations);
            const auto &probe = model_.probes[candidate.probe];
            const auto delta = sub(probe.position, p);
            const auto cosine = dot(delta, normal)/std::sqrt(candidate.distance);
            const auto radial = 1-candidate.distance/(settings_.radius*settings_.radius);
            const auto weight = cosine*radial*radial/(1+candidate.distance);
            total += weight;
            ++result.rays;
            const std::array<std::uint32_t,4> ray_key{std::bit_cast<std::uint32_t>(static_cast<float>(p[0])),
                std::bit_cast<std::uint32_t>(static_cast<float>(p[1])),std::bit_cast<std::uint32_t>(static_cast<float>(p[2])),candidate.probe};
            std::uint64_t hash=0x9e3779b97f4a7c15ULL;
            for(auto value:ray_key) {hash^=value;hash*=0xbf58476d1ce4e5b9ULL;hash^=hash>>31;}
            Ray uncached;
            auto& cached=ray_cache_.empty()?uncached:ray_cache_[hash&(ray_cache_.size()-1)];
            if(!cached.valid || cached.key!=ray_key) {
                carving::Box bounds;
                for(int a=0;a<3;++a) {
                    bounds.min[a]=std::min(p[a],probe.position[a]);
                    bounds.max[a]=std::max(p[a],probe.position[a]);
                }
                cut_index_.query(bounds,cut_candidates_,[&](auto n){consume(work_,settings_.mesh.operations,n);});
                nearby_cuts_.clear();
                for(auto id:cut_candidates_) nearby_cuts_.push_back(visibility_cuts_[id]);
                cached.visible=visible_with_scratch(hull,nearby_cuts_,p,probe.position,work_,settings_.mesh.operations,openings_);
                cached.key=ray_key;cached.valid=true;
            }
            if (!cached.visible) {
                ++result.occluded;
                continue;
            }
            for (std::size_t i = probe.values.first; i < std::size_t(probe.values.first)+probe.values.count; ++i) {
                consume(work_, settings_.mesh.operations);
                const auto &value = model_.values[i];
                for (int axis = 0; axis < 3; ++axis) coefficients[value.style][axis] += value.rgb[axis]*weight;
            }
        }
        surface::Range range{static_cast<std::uint32_t>(result.values.size()), 0};
        if (total > 0) for (std::uint32_t style = 0; style < 255; ++style) {
            const auto &rgb = coefficients[style];
            if (rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0) continue;
            if (result.values.size() >= settings_.values) throw std::length_error("Cavity light buffer capacity");
            result.values.push_back({{static_cast<float>(rgb[0]/total), static_cast<float>(rgb[1]/total),
                                      static_cast<float>(rgb[2]/total)}, style});
            ++range.count;
        }
        ++result.samples;
        return range;
    }
  private:
    struct Region {
        std::vector<carving::HullNode> nodes;
        std::int32_t root = -1;
    };
    carving::HullView local_hull(Point point) {
        if (model_.hull.root < 0 || heap_.empty()) return model_.hull;
        std::array<int,3> key;
        carving::Box bounds;
        for (int axis=0;axis<3;++axis) {
            key[axis]=static_cast<int>(std::floor(point[axis]/64));
            bounds.min[axis]=key[axis]*64.0-settings_.radius;
            bounds.max[axis]=(key[axis]+1)*64.0+settings_.radius;
        }
        if (const auto found=regions_.find(key); found!=regions_.end())
            return {model_.hull.planes,found->second.nodes,found->second.root};
        // Small immutable neighbourhood trees omit only planes that cannot
        // intersect ANY candidate ray in this tile. This avoids walking the
        // distant map root for every sample/probe pair. Bound retained scratch
        // to 4096 regions and 262144 nodes; overflow uses the original BSP.
        constexpr std::size_t max_nodes=262144;
        if (regions_.size()>=4096 || regions_full_ || region_nodes_>=max_nodes) return model_.hull;
        Region region;
        bool overflow=false;
        std::vector<std::int32_t> remap(model_.hull.nodes.size(),std::numeric_limits<std::int32_t>::min());
        const auto copy=[&](auto&& self,std::int32_t index)->std::int32_t {
            if (overflow) return -1; // The incomplete region is discarded below.
            if (index<0) return index;
            if (remap[index]!=std::numeric_limits<std::int32_t>::min()) return remap[index];
            consume(work_,settings_.mesh.operations);
            const auto& node=model_.hull.nodes[index];
            const auto& plane=model_.hull.planes[node.plane];
            Point near{},far{};
            double scale=1+std::abs(plane.distance);
            for (int axis=0;axis<3;++axis) {
                near[axis]=plane.normal[axis]>=0?bounds.min[axis]:bounds.max[axis];
                far[axis]=plane.normal[axis]>=0?bounds.max[axis]:bounds.min[axis];
                scale+=std::max(std::abs(near[axis]),std::abs(far[axis]))*std::abs(plane.normal[axis]);
            }
            // Retain touching/near-plane nodes, including floating-point
            // uncertainty in the original distance+speed*t ray traversal.
            const double low=dot(near,plane.normal)-plane.distance, high=dot(far,plane.normal)-plane.distance;
            if (low>scale*1e-9) return remap[index]=self(self,node.children[0]);
            if (high<-scale*1e-9) return remap[index]=self(self,node.children[1]);
            const auto front=self(self,node.children[0]),back=self(self,node.children[1]);
            if (front==back) return remap[index]=front;
            if (region.nodes.size()>=max_nodes-region_nodes_) {overflow=true;return -1;}
            const auto result=static_cast<std::int32_t>(region.nodes.size());
            region.nodes.push_back({node.plane,{front,back}});
            return remap[index]=result;
        };
        region.root=copy(copy,model_.hull.root);
        if (overflow) {regions_full_=true;return model_.hull;}
        region_nodes_+=region.nodes.size();
        const auto& retained=regions_.emplace(key,std::move(region)).first->second;
        return {model_.hull.planes,retained.nodes,retained.root};
    }
    void split(std::size_t begin, std::size_t end, unsigned depth) {
        if (begin == end) return;
        consume(work_, settings_.mesh.operations, end-begin);
        const auto mid = begin+(end-begin)/2, axis = std::size_t(depth%3);
        std::nth_element(order_.begin()+begin, order_.begin()+mid, order_.begin()+end,
            [&](auto a, auto b) {
                const auto pa = model_.probes[a].position[axis], pb = model_.probes[b].position[axis];
                return pa != pb ? pa < pb : a < b;
            });
        split(begin, mid, depth+1); split(mid+1, end, depth+1);
        auto& bounds=bounds_[mid];
        bounds.min=bounds.max=model_.probes[order_[mid]].position;
        const auto merge=[&](std::size_t child) {
            for(int axis=0;axis<3;++axis) {
                bounds.min[axis]=std::min(bounds.min[axis],bounds_[child].min[axis]);
                bounds.max[axis]=std::max(bounds.max[axis],bounds_[child].max[axis]);
            }
        };
        if(begin<mid)merge(begin+(mid-begin)/2);
        if(mid+1<end)merge(mid+1+(end-mid-1)/2);
    }
    void query(std::size_t begin, std::size_t end, unsigned depth, Point p, Point normal) {
        if (begin == end) return;
        consume(work_, settings_.mesh.operations);
        const auto mid = begin+(end-begin)/2, index = order_[mid];
        // A split plane alone cannot prune a subtree wholly behind a sample,
        // or far away along another axis. Bound the complete probe subtree
        // while preserving exact nearest-distance/index tie selection.
        const auto& bounds=bounds_[mid];
        Point closest{},front{};
        for(int axis=0;axis<3;++axis) {
            closest[axis]=std::clamp(p[axis],bounds.min[axis],bounds.max[axis])-p[axis];
            front[axis]=(normal[axis]>=0?bounds.max[axis]:bounds.min[axis])-p[axis];
        }
        const auto maximum=heap_.size()==settings_.neighbours?heap_.front().distance:settings_.radius*settings_.radius;
        if(dot(closest,closest)>maximum || dot(front,normal)<=0)return;
        const auto delta = sub(model_.probes[index].position, p);
        const auto distance = dot(delta, delta);
        const Candidate candidate{distance, index};
        if (distance > 0 && distance < settings_.radius*settings_.radius && dot(delta, normal) > 0) {
            if (heap_.size() < settings_.neighbours) {
                heap_.push_back(candidate); std::push_heap(heap_.begin(), heap_.end());
            } else if (candidate < heap_.front()) {
                std::pop_heap(heap_.begin(), heap_.end()); heap_.back() = candidate;
                std::push_heap(heap_.begin(), heap_.end());
            }
        }
        const auto side = delta[depth%3];
        if (side >= 0) query(begin, mid, depth+1, p, normal);
        else query(mid+1, end, depth+1, p, normal);
        const auto range = heap_.size() == settings_.neighbours ? heap_.front().distance : settings_.radius*settings_.radius;
        if (side*side <= range) {
            if (side >= 0) query(mid+1, end, depth+1, p, normal);
            else query(begin, mid, depth+1, p, normal);
        }
    }
    const Model &model_;
    const Settings &settings_;
    std::size_t &work_;
    const std::atomic_bool* cancelled_;
    std::vector<std::uint32_t> order_;
    std::vector<carving::Box> bounds_;
    std::vector<Candidate> heap_;
    // Only ray openings use the compact union. Keep the original records for
    // geometry, material ownership and authoritative revision accounting.
    std::vector<carving::Box> visibility_cuts_;
    carving::CutIndex cut_index_;
    std::vector<std::size_t> cut_candidates_;
    std::vector<carving::Box> nearby_cuts_;
    std::vector<Interval> openings_;
    std::map<std::array<int,3>,Region> regions_;
    std::size_t region_nodes_ = 0;
    bool regions_full_ = false;
    // The same position can be sampled with several face normals. Visibility
    // is normal-independent; exact-key replacement keeps this scratch bounded
    // and never reuses an answer across models, cuts or preparation revisions.
    struct Ray { std::array<std::uint32_t,4> key{}; bool valid{},visible{}; };
    std::vector<Ray> ray_cache_;
};
using Key = std::array<std::uint32_t, 6>;
struct Hash { std::size_t operator()(const Key &key) const {
    // Integral voxel positions/normals share low mantissa bits. Multiplying
    // whole float words as FNV left those bits identical in MSVC's power-of-two
    // buckets, turning the sample cache into a long linear chain after TNT.
    std::uint32_t result = 2166136261u;
    for (auto v : key) {
        v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
        result = (std::rotl(result, 5) ^ v) * 0x9e3779b1u;
    }
    return result;
}};
Key key(const surface::Vertex &v, const surface::Frame &frame) {
    Key result{};
    for (int i = 0; i < 3; ++i) {
        result[i] = std::bit_cast<std::uint32_t>(v.pos[i] == 0 ? 0.f : v.pos[i]);
        result[i+3] = std::bit_cast<std::uint32_t>(frame.normal[i] == 0 ? 0.f : frame.normal[i]);
    }
    return result;
}
bool box_less(const carving::Box& a, const carving::Box& b) {
    return a.min != b.min ? a.min < b.min : a.max < b.max;
}
struct Reuse {
    const SampleCache* previous{};
    std::vector<carving::Box> changed;
};
// Compare complete numeric inputs, not a hash or a model pointer that a new
// map can recycle. New probes must preserve the relative order of all old
// probes: nearest-distance ties and coefficient summation then remain exact.
Reuse reuse(const Model& model, const Settings& settings, std::size_t& work,
            const std::atomic_bool* cancelled) {
    const auto* old = settings.cache_bytes ? model.previous.get() : nullptr;
    if (!old || old->entries.empty() || old->first_face != model.first_face || old->face_count != model.face_count ||
        old->radius != settings.radius || old->neighbours != settings.neighbours || old->root != model.hull.root ||
        old->planes.size() != model.hull.planes.size() || old->nodes.size() != model.hull.nodes.size() ||
        old->cuts.size() > model.cuts.size() || old->probes.size() > model.probes.size()) return {};
    const auto charge = [&](std::size_t n) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Cavity lighting cancelled");
        consume(work,settings.mesh.operations,n);
    };
    charge(old->planes.size()+old->nodes.size());
    if (!std::equal(old->planes.begin(),old->planes.end(),model.hull.planes.begin(),
            [](const auto& a,const auto& b){return a.normal==b.normal && a.distance==b.distance;}) ||
        !std::equal(old->nodes.begin(),old->nodes.end(),model.hull.nodes.begin(),
            [](const auto& a,const auto& b){return a.plane==b.plane && a.children==b.children;})) return {};
    std::vector<carving::Box> sorted(model.cuts.begin(),model.cuts.end());
    charge(sorted.size()*std::max<std::size_t>(1,std::bit_width(sorted.size())));
    std::sort(sorted.begin(),sorted.end(),box_less);
    Reuse result{old,{}};
    std::size_t at=0;
    for (const auto& cut:sorted) {
        charge(1);
        if (at<old->cuts.size() && cut.min==old->cuts[at].min && cut.max==old->cuts[at].max) ++at;
        else result.changed.push_back(cut);
    }
    if (at!=old->cuts.size()) return {}; // Removed/replaced cuts require a full calculation.
    at=0;
    for (const auto& probe:model.probes) {
        charge(1+probe.values.count);
        bool same=false;
        if (at<old->probes.size()) {
            const auto& before=old->probes[at];
            same=probe.position==before.position && probe.values.count==before.values.count &&
                std::equal(old->probe_values.begin()+before.values.first,
                    old->probe_values.begin()+before.values.first+before.values.count,
                    model.values.begin()+probe.values.first);
        }
        if (same) ++at;
        else result.changed.push_back({probe.position,probe.position});
    }
    if (at!=old->probes.size()) return {}; // Changed values, removal or reordered ties.
    // Every lighting ray and every eligible probe is within radius of its
    // sample. Invalidate a conservative cube around additions, including
    // touching boundaries and rounding uncertainty. Far samples are unchanged.
    for (auto& bounds:result.changed) for (int axis=0;axis<3;++axis) {
        bounds.min[axis]-=settings.radius+.000001;
        bounds.max[axis]+=settings.radius+.000001;
    }
    return result;
}
std::shared_ptr<const SampleCache> retain(const Model& model, const Settings& settings,
    const std::vector<SampleCache::Entry>& samples, Prepared& result,
    const std::atomic_bool* cancelled) {
    if (!settings.cache_bytes || samples.empty()) return {};
    const std::uint64_t inputs=sizeof(SampleCache)+model.hull.planes.size()*sizeof(carving::Plane)+
        model.hull.nodes.size()*sizeof(carving::HullNode)+model.cuts.size()*sizeof(carving::Box)+
        model.probes.size()*sizeof(Probe)+model.values.size()*sizeof(Value);
    if (inputs>=settings.cache_bytes) return {};
    std::size_t count=0, values=0;
    for (const auto& sample:samples) {
        if (inputs+(count+1)*sizeof(SampleCache::Entry)+(values+sample.values.count)*sizeof(Value)>settings.cache_bytes) break;
        ++count; values+=sample.values.count;
    }
    if (!count) return {};
    consume(result.operations,settings.mesh.operations,model.hull.planes.size()+model.hull.nodes.size()+
        model.probes.size()+model.values.size()+model.cuts.size()*std::max<std::size_t>(1,std::bit_width(model.cuts.size()))+
        count*std::max<std::size_t>(1,std::bit_width(count))+values);
    auto cache=std::make_shared<SampleCache>();
    cache->first_face=model.first_face;cache->face_count=model.face_count;
    cache->radius=settings.radius;cache->neighbours=settings.neighbours;cache->root=model.hull.root;
    cache->planes.assign(model.hull.planes.begin(),model.hull.planes.end());
    cache->nodes.assign(model.hull.nodes.begin(),model.hull.nodes.end());
    cache->cuts.assign(model.cuts.begin(),model.cuts.end());
    std::sort(cache->cuts.begin(),cache->cuts.end(),box_less);
    cache->probes.assign(model.probes.begin(),model.probes.end());
    cache->probe_values.assign(model.values.begin(),model.values.end());
    cache->entries.reserve(count);cache->values.reserve(values);
    for (std::size_t i=0;i<count;++i) {
        if (cancelled && cancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Cavity lighting cancelled");
        auto entry=samples[i];
        entry.values.first=static_cast<std::uint32_t>(cache->values.size());
        cache->values.insert(cache->values.end(),result.values.begin()+samples[i].values.first,
            result.values.begin()+samples[i].values.first+entry.values.count);
        cache->entries.push_back(entry);
    }
    std::sort(cache->entries.begin(),cache->entries.end(),[](const auto& a,const auto& b){return a.key<b.key;});
    if (cache_bytes(*cache)>settings.cache_bytes) return {}; // Account actual retained capacities.
    return cache;
}
} // namespace

bool visible(carving::HullView hull, std::span<const carving::Box> cuts, Point start, Point end,
             std::size_t &operations, std::size_t limit) {
    std::vector<Interval> openings;
    return visible_with_scratch(hull, cuts, start, end, operations, limit, openings);
}

Prepared prepare(const cavity::Prepared &geometry, std::span<const Model> models, Settings settings,
                 const std::atomic_bool* cancelled) {
    if (!geometry.mesh || !std::isfinite(settings.radius) || settings.radius <= 0 || settings.radius > 65536 ||
        !std::isfinite(settings.spacing) || settings.spacing <= 0 || !settings.neighbours || settings.neighbours > 256 ||
        settings.values > std::numeric_limits<std::uint32_t>::max() ||
        settings.mesh.vertices > std::numeric_limits<std::uint32_t>::max() ||
        settings.mesh.indices > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Cavity lighting settings");
    Prepared result{geometry.mesh, geometry.draws, {}, {}, 0, 0, 0, 0};
    if (geometry.draws.empty()) return result;
    const auto &source = *geometry.mesh;
    if (source.vertices.size() != source.frames.size() || source.vertices.size() > settings.mesh.vertices ||
        source.indices.size() > settings.mesh.indices) throw std::length_error("Cavity lighting mesh capacity");
    auto mesh = std::make_shared<surface::Mesh>(source);
    result.vertices.resize(mesh->vertices.size());
    std::vector<bool> handled(result.draws.size());
    for (const auto &model : models) {
        if(cancelled && cancelled->load(std::memory_order_relaxed))throw std::runtime_error("Cavity lighting cancelled");
        consume(result.operations, settings.mesh.operations, carving::validate_hull(model.hull,
            {settings.mesh.fragments, settings.mesh.operations-result.operations}));
        for (const auto &cut : model.cuts) {
            valid(cut.min); valid(cut.max);
            for (int i = 0; i < 3; ++i) if (cut.min[i] >= cut.max[i]) throw std::invalid_argument("Cavity lighting cut");
        }
        Field field(model, settings, result.operations, cancelled);
        std::unordered_map<Key, surface::Range, Hash> cache;
        const auto prior=reuse(model,settings,result.operations,cancelled);
        const carving::CutIndex changed(prior.changed,[&](auto n){consume(result.operations,settings.mesh.operations,n);});
        std::vector<SampleCache::Entry> samples;
        for (std::size_t d = 0; d < result.draws.size(); ++d) {
            auto &draw = result.draws[d];
            if (draw.first_face != model.first_face || draw.face_count != model.face_count) continue;
            if (handled[d]) throw std::invalid_argument("Duplicate cavity lighting model");
            handled[d] = true;
            const auto old = draw.indices;
            if (old.first > source.indices.size() || old.count > source.indices.size()-old.first || old.count%3)
                throw std::invalid_argument("Cavity lighting draw range");
            draw.indices = {static_cast<std::uint32_t>(mesh->indices.size()), 0};
            for (std::size_t at = old.first; at < std::size_t(old.first)+old.count; at += 3) {
                std::array<surface::Vertex, 3> triangle;
                for (int i = 0; i < 3; ++i) {
                    if (source.indices[at+i] >= source.vertices.size()) throw std::invalid_argument("Cavity lighting vertex");
                    triangle[i] = source.vertices[source.indices[at+i]];
                }
                const auto &frame = source.frames[source.indices[at]];
                const auto edge1 = sub(point(triangle[1].pos), point(triangle[0].pos));
                const auto edge2 = sub(point(triangle[2].pos), point(triangle[0].pos));
                const Point original_cross{edge1[1]*edge2[2]-edge1[2]*edge2[1],
                    edge1[2]*edge2[0]-edge1[0]*edge2[2], edge1[0]*edge2[1]-edge1[1]*edge2[0]};
                // Preserve input winding; GoldSrc front-face culling uses
                // triangles whose cross product opposes the lighting normal.
                const auto original_area = dot(original_cross, point(frame.normal));
                if (!std::isfinite(original_area) || original_area == 0)
                    throw std::invalid_argument("Cavity lighting source winding");
                double length = 0;
                for (int i = 0; i < 3; ++i) {
                    const auto edge = sub(point(triangle[i].pos), point(triangle[(i+1)%3].pos));
                    length = std::max(length, std::sqrt(dot(edge, edge)));
                }
                const auto steps = std::max(1., std::ceil(length/settings.spacing));
                if (!std::isfinite(steps) || steps > 1024) throw std::length_error("Cavity lighting subdivision budget");
                const auto n = static_cast<std::uint32_t>(steps);
                const auto count = std::size_t(n+1)*(n+2)/2;
                if (count > settings.mesh.vertices-mesh->vertices.size() ||
                    std::size_t(n)*n*3 > settings.mesh.indices-mesh->indices.size())
                    throw std::length_error("Cavity lighting refinement capacity");
                consume(result.operations, settings.mesh.operations, count+std::size_t(n)*n*3);
                const auto base = static_cast<std::uint32_t>(mesh->vertices.size());
                for (std::uint32_t i = 0; i <= n; ++i) for (std::uint32_t j = 0; j <= n-i; ++j) {
                    const double weights[]{double(n-i-j)/n, double(i)/n, double(j)/n};
                    surface::Vertex vertex{};
                    for (int axis = 0; axis < 3; ++axis) {
                        double coordinate = 0;
                        for (int k = 0; k < 3; ++k) coordinate += weights[k]*triangle[k].pos[axis];
                        vertex.pos[axis] = static_cast<float>(coordinate);
                    }
                    for (int axis = 0; axis < 2; ++axis) {
                        double uv = 0, lm = 0;
                        for (int k = 0; k < 3; ++k) { uv += weights[k]*triangle[k].texcoord[axis]; lm += weights[k]*triangle[k].lightmaptexcoord[axis]; }
                        vertex.texcoord[axis] = static_cast<float>(uv); vertex.lightmaptexcoord[axis] = static_cast<float>(lm);
                    }
                    const auto sample_key = key(vertex, frame);
                    auto it = cache.find(sample_key);
                    if (it == cache.end()) {
                        if (cancelled && cancelled->load(std::memory_order_relaxed)) throw std::runtime_error("Cavity lighting cancelled");
                        const auto rays=result.rays,occluded=result.occluded;
                        const auto p=point(vertex.pos);
                        const SampleCache::Entry* previous=nullptr;
                        if (prior.previous && !changed.any([&](const auto& bounds) {
                                for(int a=0;a<3;++a) if(p[a]<bounds.min[a] || p[a]>bounds.max[a])return false;
                                return true;
                            },[&](auto n){consume(result.operations,settings.mesh.operations,n);})) {
                            const auto& entries=prior.previous->entries;
                            consume(result.operations,settings.mesh.operations,std::bit_width(entries.size()));
                            const auto found=std::lower_bound(entries.begin(),entries.end(),sample_key,
                                [](const auto& entry,const auto& k){return entry.key<k;});
                            if(found!=entries.end() && found->key==sample_key)previous=&*found;
                        }
                        surface::Range range;
                        if (previous) {
                            range={static_cast<std::uint32_t>(result.values.size()),previous->values.count};
                            if(range.count>settings.values-result.values.size())throw std::length_error("Cavity light buffer capacity");
                            const auto& values=prior.previous->values;
                            result.values.insert(result.values.end(),values.begin()+previous->values.first,
                                values.begin()+previous->values.first+range.count);
                            ++result.samples; ++result.reused_samples;
                            result.rays+=previous->rays;result.occluded+=previous->occluded;
                        } else range=field.sample(p,point(frame.normal),result);
                        it=cache.emplace(sample_key,range).first;
                        // Bounded transient records; skipped entries simply calculate again next revision.
                        if (settings.cache_bytes && samples.size()<settings.cache_bytes/sizeof(SampleCache::Entry))
                            samples.push_back({sample_key,range,static_cast<std::uint32_t>(result.rays-rays),static_cast<std::uint32_t>(result.occluded-occluded)});
                    }
                    mesh->vertices.push_back(vertex); mesh->frames.push_back(frame); result.vertices.push_back(it->second);
                    for (int axis = 0; axis < 3; ++axis) {
                        draw.bounds.min[axis] = std::min(draw.bounds.min[axis], double(vertex.pos[axis]));
                        draw.bounds.max[axis] = std::max(draw.bounds.max[axis], double(vertex.pos[axis]));
                    }
                }
                const auto row = [n, base](std::uint32_t i, std::uint32_t j) { return base+i*(2*n+3-i)/2+j; };
                const auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                    const auto ab = sub(point(mesh->vertices[b].pos), point(mesh->vertices[a].pos));
                    const auto ac = sub(point(mesh->vertices[c].pos), point(mesh->vertices[a].pos));
                    const Point cross{ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]};
                    const auto area = dot(cross, point(frame.normal));
                    if (area * original_area < 0) throw std::invalid_argument("Cavity lighting refinement reverses winding");
                    if (area != 0) mesh->indices.insert(mesh->indices.end(), {a,b,c});
                };
                for (std::uint32_t i = 0; i < n; ++i) for (std::uint32_t j = 0; j < n-i; ++j) {
                    emit(row(i,j), row(i+1,j), row(i,j+1));
                    if (j+1 < n-i) emit(row(i+1,j), row(i+1,j+1), row(i,j+1));
                }
            }
            draw.indices.count = static_cast<std::uint32_t>(mesh->indices.size())-draw.indices.first;
            draw.needs_lighting = false;
        }
        if (auto retained=retain(model,settings,samples,result,cancelled))
            result.caches.push_back({model.first_face,model.face_count,std::move(retained)});
    }
    if (std::find(handled.begin(), handled.end(), false) != handled.end()) throw std::invalid_argument("Missing cavity lighting model");
    result.mesh = std::move(mesh);
    return result;
}
} // namespace goldcraft::lighting
