#pragma once

namespace goldcraft::amxx_effects {

// Additive calls from the corresponding existing GoldCraft AMXX entry points.
// This candidate supplies no AMXX export/configuration of its own, preventing
// accidental replacement of the production form natives or their listeners.
void OnAttach();
void OnPluginsLoaded();
void OnPluginsUnloading();
void OnDetach();
void OnFrame();

} // namespace goldcraft::amxx_effects
