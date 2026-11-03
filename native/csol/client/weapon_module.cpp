// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_module.hpp"
#include <algorithm>
#include <stdexcept>

namespace csol::client
{
namespace
{
struct Registry
{
    std::array<const WeaponModule*,WeaponIdLimit> byId{};
    std::array<const WeaponModule*,WeaponIdLimit> ordered{};
    std::size_t count{};
    bool invalid{};
};
Registry& Modules() noexcept
{
    static constinit Registry registry;
    return registry;
}
}
WeaponModuleRegistration::WeaponModuleRegistration(const WeaponModule& module) noexcept
{
    auto& registry=Modules();
    if(module.ids.empty() || registry.count==registry.ordered.size())
    {registry.invalid=true;return;}
    for(std::size_t i=0;i<module.ids.size();++i)
    {
        const int id=module.ids[i];
        if(!ExtendedWeaponId(id) || registry.byId[id] ||
            std::find(module.ids.begin(),module.ids.begin()+i,id)!=module.ids.begin()+i)
        {registry.invalid=true;return;}
    }
    auto begin=registry.ordered.begin(),end=begin+registry.count;
    const auto position=std::lower_bound(begin,end,module.order,
        [](const WeaponModule* item,int order){return item->order<order;});
    if(position!=end && (*position)->order==module.order)
    {registry.invalid=true;return;}
    std::move_backward(position,end,end+1);
    *position=&module;++registry.count;
    for(int id:module.ids)registry.byId[id]=&module;
}
std::span<const WeaponModule* const> WeaponModules() noexcept
{
    auto& registry=Modules();
    return {registry.ordered.data(),registry.count};
}
const WeaponModule* FindWeaponModule(int id) noexcept
{
    return ValidWeaponId(id)?Modules().byId[id]:nullptr;
}
void InitializeWeaponModules()
{
    if(Modules().invalid)throw std::runtime_error("CSOL client weapon registration conflicts");
    for(const auto* module:WeaponModules())if(module->initialize)module->initialize();
}
void ResetWeaponModules()
{
    for(const auto* module:WeaponModules())if(module->reset)module->reset();
    for(const auto* module:WeaponModules())if(module->afterReset)module->afterReset();
}
void WeaponModulesVideoInit()
{
    for(const auto* module:WeaponModules())if(module->videoInit)module->videoInit();
}
}
