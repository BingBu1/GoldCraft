#pragma once
#include <metahook.h>

namespace goldcraft::hud_sprites {
using Loader = decltype(cl_enginefunc_t::pfnSPR_GetList);
void bind(Loader original);
client_sprite_t* list(char* path, int* count);
}
