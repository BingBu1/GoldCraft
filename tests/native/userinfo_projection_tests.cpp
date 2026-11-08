// SPDX-License-Identifier: GPL-3.0-or-later
#include "goldcraft/userinfo.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    using namespace goldcraft::userinfo;
    // Handshake parsing precedes the overflow-field command queue. A resource
    // decoder capability must survive the same 224-byte projection as connect.
    const std::string fields =
        "\\name\\GoldCraft_B\\gc_precache\\3\\_gcpe\\1\\gc_brushes\\1"
        "\\_gcf\\1\\_gcu\\1\\model\\gordon\\rate\\100000\\cl_updaterate\\102"
        "\\cl_lw\\1\\cl_lc\\1\\topcolor\\30\\bottomcolor\\6\\_vgui_menus\\0"
        "\\optional_a\\abcdefghijklmnopqrstuvwx\\optional_b\\abcdefghijklmnopqrstuvwx\\x\\1";
    unsigned cases = 0;
    for (const char* sync : {"0", "1"}) {
        Storage storage;
        const auto original = fields + "\\gc_resource_sync\\" + sync;
        assert(storage.assign(original));
        std::array<char, legacy_capacity> legacy{};
        storage.publish(legacy.data(), legacy_capacity - identity_reserve);
        assert(std::strlen(legacy.data()) < legacy_capacity - identity_reserve);
        assert(value(legacy.data(), "gc_precache") == "3");
        assert(value(legacy.data(), "gc_resource_sync") == sync);
        assert(value(legacy.data(), "_gcu") == "1");
        assert(value(legacy.data(), "name") == "GoldCraft_B");
        assert(std::string_view(storage.full.data()) == original);
        unsigned occurrences = 0;
        assert(goldcraft::userinfo::fields(legacy.data(), [&](auto key, auto) {
            if (key == "gc_resource_sync") ++occurrences;
        }));
        assert(occurrences == 1);
        storage.reconcile(legacy.data());
        assert(value(storage.full.data(), "gc_resource_sync") == sync);
        assert(value(storage.full.data(), "optional_b") == "abcdefghijklmnopqrstuvwx");
        ++cases;
    }
    std::array<char, legacy_capacity> legacy{};
    project(legacy.data(), legacy.size(), fields);
    assert(value(legacy.data(), "gc_resource_sync").empty());
    ++cases; // A legacy client does not acquire an unadvertised capability.
    project(legacy.data(), legacy.size(), "\\name\\Player\\gc_resource_sync\\1");
    assert(value(legacy.data(), "gc_resource_sync") == "1");
    assert(value(legacy.data(), "gc_precache").empty());
    ++cases; // Projection cannot manufacture the required media parser.
    std::cout << "userinfo projection: " << cases << " lifecycle cases passed\n";
}
