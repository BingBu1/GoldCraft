#include "goldcraft/edited_hull.hpp"
#include <iomanip>
#include <iostream>
#include <stdexcept>

// Test-only cross-language oracle. Input contains values, never engine pointers.
// Java tests feed the same real BSP planes/nodes/cuts to this x86 native core.
using namespace goldcraft::carving;
namespace {
std::size_t count(std::size_t limit) {
    std::size_t n = 0;
    if (!(std::cin >> n) || n > limit) throw std::invalid_argument("Probe count");
    return n;
}
Point point() {
    Point p{};
    if (!(std::cin >> p[0] >> p[1] >> p[2])) throw std::invalid_argument("Probe point");
    return p;
}
struct Hull {
    std::vector<Plane> planes;
    std::vector<HullNode> nodes;
    std::int32_t root;
    Hull() {
        const auto plane_count = count(65535);
        for (std::size_t i = 0; i < plane_count; ++i) {
            const auto normal = point(); double distance = 0;
            if (!(std::cin >> distance)) throw std::invalid_argument("Probe plane");
            planes.push_back({normal, distance});
        }
        const auto node_count = count(32767);
        for (std::size_t i = 0; i < node_count; ++i) {
            HullNode node{};
            if (!(std::cin >> node.plane >> node.children[0] >> node.children[1])) throw std::invalid_argument("Probe node");
            nodes.push_back(node);
        }
        if (!(std::cin >> root)) throw std::invalid_argument("Probe root");
    }
    HullView view() const { return {planes, nodes, root}; }
};
}
int main() {
    try {
        std::cout << std::setprecision(17);
        Hull point_hull, body_hull;
        Box body{point(), point()};
        const auto cases = count(100);
        for (std::size_t i = 0; i < cases; ++i) {
            std::vector<Box> cuts;
            const auto n = count(1024);
            for (std::size_t j = 0; j < n; ++j) cuts.push_back({point(), point()});
            EditedHull edit(point_hull.view(), body_hull.view(), body, cuts);
            const auto queries = count(100000);
            for (std::size_t j = 0; j < queries; ++j) {
                const auto a = point(), b = point();
                const auto trace = edit.trace(a, b);
                std::cout << trace.fraction << ' ' << trace.start_solid << ' ' << trace.all_solid
                          << ' ' << trace.in_open << ' ' << trace.in_water;
                for (const auto value : trace.plane.normal) std::cout << ' ' << value;
                std::cout << ' ' << trace.plane.distance << ' ' << edit.contents(a) << ' ' << edit.contents(b) << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
