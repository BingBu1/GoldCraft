// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <com_model.h>
#include "resource_sprite_handles.hpp"
#include <cstring>
#include <new>

namespace goldcraft::resource_sprite_handles {
static_assert(sizeof(Entry) == 12 && sizeof(void*) == 4);
namespace {
const char* Validate(const Binding& binding) {
    if (!binding.entries || !binding.capacity || !binding.load || *binding.capacity < 0 ||
        (*binding.capacity && !*binding.entries)) return "Native sprite registry is unavailable";
    return nullptr;
}
}
const char* Capture(const Binding& binding, std::vector<Handle>& output) {
    if (const auto* error = Validate(binding)) return error;
    std::vector<Handle> values;
    try {
        for (int i = 0; i < *binding.capacity; ++i) {
            const auto& entry = (*binding.entries)[i];
            if (!entry.name) {
                if (entry.model) return "Native sprite registry entry has no name";
                continue;
            }
            if (!*entry.name || !entry.model || entry.model->type != mod_sprite)
                return "Native sprite registry entry has no sprite model";
            values.push_back({i + 1, entry.name});
        }
    } catch (const std::bad_alloc&) { return "Cannot snapshot native sprite handles"; }
    output = std::move(values);
    return nullptr;
}
const char* Reload(const Binding& binding, const std::vector<Handle>& handles) {
    if (const auto* error = Validate(binding)) return error;
    // Validate the entire borrowed generation before invoking the first loader.
    for (const auto& handle : handles) {
        if (handle.value <= 0 || handle.value > *binding.capacity || handle.name.empty())
            return "Native sprite handle generation changed";
        const auto& current = (*binding.entries)[handle.value - 1];
        if (!current.name || handle.name != current.name)
            return "Native sprite handle name changed";
    }
    for (const auto& handle : handles) {
        if (const auto* error = Validate(binding)) return error;
        if (handle.value > *binding.capacity || !(*binding.entries)[handle.value - 1].name ||
            handle.name != (*binding.entries)[handle.value - 1].name)
            return "Native sprite registry changed during reload";
        const int loaded = binding.load(handle.name.c_str());
        if (const auto* error = Validate(binding)) return error;
        if (loaded != handle.value || loaded <= 0 || loaded > *binding.capacity)
            return "Native sprite loader did not retain its handle";
        const auto& current = (*binding.entries)[loaded - 1];
        if (!current.name || handle.name != current.name || !current.model ||
            current.model->type != mod_sprite || !current.model->cache.data || current.frames <= 0)
            return "Native sprite loader did not rebuild its resource";
    }
    return nullptr;
}
}
