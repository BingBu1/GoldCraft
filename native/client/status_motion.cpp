#include <metahook.h>
#include <pm_defs.h>
#include <pm_movevars.h>
#include "status_motion.hpp"
#include "goldcraft/status_motion.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace goldcraft::client_motion {
namespace {
metahook_api_t* api{};
using Scope=status_motion::MovementScope<playermove_t>;
using Function=void(__cdecl*)();
std::array<Function,3> originals{};
std::array<hook_t*,3> hooks{};
bool enabled=false;std::string failure;
void __cdecl categorize(){Scope::categorize(originals[0]);}
void __cdecl jump(){Scope::jump(originals[1]);}
void __cdecl water(){Scope::water(originals[2]);}
}
bool install(metahook_api_t* value){
    if(enabled)return true;api=value;
    try{
        auto* module=api->GetClientBase();std::uint64_t crc=0;
        if(!module||api->GetModuleCRC64(module,&crc)!=MH_GAMESYMBOL_OK||crc!=0x124d6034a6b28b40ULL)
            throw std::runtime_error("motion requires the verified CS client");
        const char* names[]{"CategorizePosition","Jump","WaterMove"};
        std::array<void*,3> targets{};const Function replacement[]{categorize,jump,water};
        for(unsigned i=0;i<3;++i){
            const auto symbol=std::string("GoldCraftMotion_")+names[i];
            if(api->ResolveGameSymbol(module,symbol.c_str(),MH_GAMESYMBOL_KIND_FUNCTION,&targets[i])!=MH_GAMESYMBOL_OK||!targets[i])
                throw std::runtime_error("motion symbol unavailable: "+symbol);
        }
        for(unsigned i=0;i<3;++i){
            hooks[i]=api->InlineHook(targets[i],reinterpret_cast<void*>(replacement[i]),reinterpret_cast<void**>(&originals[i]));
            if(!hooks[i])throw std::runtime_error("motion hook failed");
        }
        enabled=true;failure.clear();
    }catch(const std::exception& e){shutdown();failure=e.what();}
    return enabled;
}
void shutdown(){for(auto& hook:hooks)if(hook){api->UnHook(hook);hook=nullptr;}enabled=false;}
bool ready(){return enabled;}
const char* error(){return failure.c_str();}
}
