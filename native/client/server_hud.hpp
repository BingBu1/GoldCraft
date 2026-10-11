#pragma once
#include "goldcraft/form_menu.hpp"
#include <metahook.h>

namespace goldcraft::server_hud {
void initialize(cl_enginefunc_t* engine);
void reset();
void video_reset();
void death(std::span<const std::uint8_t> message,double now);
void team(std::span<const std::uint8_t> message);
void draw(double now,const FormMenu& menu,bool show_menu);
std::uint64_t death_messages();
std::uint64_t death_draws();
std::uint64_t menu_draws();
unsigned notices(double now);
}
