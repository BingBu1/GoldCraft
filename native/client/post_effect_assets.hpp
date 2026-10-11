#pragma once
#include <array>
namespace goldcraft::render::post_assets {
// Identities only. NightFall's All Rights Reserved shader contents remain in
// the user's installed Mod and local extracted runtime directory.
inline constexpr char nightfall_sha256[]="ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c";
struct Asset {const char* name;const char* sha256;};
inline constexpr Asset reference_vertex{"blit.vsh","87b5456f4fbf4565af12da6db03cd756d748fbbdb07f6d46f761d8ac64449d19"};
inline constexpr std::array<Asset,10> shaders{{
    {"down_sampling.fsh","f52ac09094c5778871ccfd53b117157a256835644e1598c7d42ea09635c6ae2b"},
    {"up_sampling.fsh","1e59ec8371be20e6de9aefee2d0aa0579bfe1b032ef19123f1db0e44e932bbf4"},
    {"unity_composite.fsh","cf87d4ff378f96fdafc8af8d32679f3786a85953ef282e55e6319496ab7b5365"},
    {"space_broken.fsh","9d76668d129c05a217170d39c9206e191b006c4b35b1694e508f75bf12e44e67"},
    {"chromatic_aberration.fsh","61c0105ed280fe4eed7dace3d1c89701d8ee7ca51be53cee253a8038bc666305"},
    {"air_disturbance.fsh","336c0123bc1bd5a7cdca6190ce6a21976035cc623d9c3cf80e7dceb2f629a0cf"},
    {"screen_distortion.fsh","2a710be6c5493d4971587c05ea627685826955f56de04005d3c3cc12e2194f14"},
    {"color_dispersion.fsh","08dcd85523d534a8af5db2b154146475b7d9aefe9d26ea9845d7193d149b7fa9"},
    {"black_white_simple.fsh","89432ec2c4e4565898eeece0d70f1a1d1999ce59790be13d946ebf16825d3abe"},
    {"blit.fsh","bb9351ccd461eef6a0b6780a8756691081fbc4b4e0fa34734fb1f5db37d92b18"}
}};
}
