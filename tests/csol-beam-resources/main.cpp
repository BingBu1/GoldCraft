// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_source.hpp"
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        const csol::ModelSource model{"models/ef_beamgunle_shootB.mdl","models/ef_beamgunle_shootB.mdl","models/ef_beamgunle_shootB.mdl",{},{},false};
        int checks = 0;
        auto check = [&](bool value) { ++checks; if(!value)throw std::runtime_error("texture name regression"); };
        const auto absent = [](const std::string&) { return false; };
        const auto texture = csol::ResolveTextureSource(model,"$0b_beamgunle_shootB_T4 .bmp","",absent);
        check(texture.Name == "$0b_beamgunle_shootB_T4 .bmp");
        check(texture.Resource == "models/$0b_beamgunle_shootB_T4 .tga");
        check(texture.EmbeddedFallback);
        const auto external = csol::ResolveTextureSource(model,"internal name.bmp","external name.tga",absent);
        check(external.Resource == "models/texture/external name.tga");
        for (const auto* invalid : {"../escape.bmp","sub/name.bmp","sub\\name.bmp","C:name.bmp","/name.bmp","name.bmp ","name.bmp.","name\n.bmp","","*.bmp"})
        {
            bool rejected = false;
            try { csol::ResolveTextureSource(model,invalid,"",absent); }
            catch(const std::runtime_error&) { rejected = true; }
            check(rejected);
            if (*invalid)
            {
                rejected = false;
                try { csol::ResolveTextureSource(model,"valid.bmp",invalid,absent); }
                catch(const std::runtime_error&) { rejected = true; }
                check(rejected);
            }
        }
        std::cout << "PASS: " << checks << " texture-name checks, " << sizeof(void*)*8 << "-bit process\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
