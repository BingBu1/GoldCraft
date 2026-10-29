// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "weapon_damage_runtime.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
std::string gameDirectory, output;
void (*reloadCommand)() = nullptr;
int arguments = 1, checks = 0, registrations = 0;
void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(),static_cast<std::streamsize>(text.size()));
    if (!file) throw std::runtime_error("fixture write failed");
}
std::string Override(const std::string& original, double value)
{
    rapidjson::Document d; d.Parse(original.c_str());
    d["overrides"]["beamgunle"].AddMember("ShootADamageZombie",value,d.GetAllocator());
    rapidjson::StringBuffer buffer; rapidjson::Writer writer(buffer); d.Accept(writer);
    return {buffer.GetString(),buffer.GetSize()};
}
}
int CMD_ARGC() { return arguments; }
void SERVER_PRINT(const char* text) { output += text; }
void ALERT(ALERT_TYPE, const char* format, ...)
{
    char buffer[2048]{}; va_list args; va_start(args,format);
    std::vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); output += buffer;
}
void ADD_SERVER_COMMAND(const char* name, void (*callback)())
{
    Check(std::strcmp(name,"csol_reload_beam_damage") == 0,"server command name");
    reloadCommand = callback; ++registrations;
}
void GET_GAME_DIR(char* directory) { std::strcpy(directory,gameDirectory.c_str()); }

int main(int argc,char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("expected source config and private fixture path");
        std::ifstream source(argv[1],std::ios::binary);
        if (!source) throw std::runtime_error("source config missing");
        const std::string original((std::istreambuf_iterator<char>(source)),{});
        const auto root = std::filesystem::absolute(argv[2]).lexically_normal();
        const auto file = root / "csol/weapons/beamgun_damage.json";
        // Exclusive creation prevents this test from deleting a preexisting file.
        Check(!std::filesystem::exists(root),"fixture directory already exists");
        std::filesystem::create_directories(file.parent_path());
        gameDirectory = root.string();
        CSOL_InitBeamDamage();
        Check(registrations == 1 && reloadCommand,"registered server-only callback");
        Check(!CSOL_BeamDamageReady(),"initially unavailable");
        Check(!CSOL_ReloadBeamDamage() && !CSOL_BeamDamageReady(),"missing initial file stays unavailable");
        Check(output.find("remain unavailable") != std::string::npos,"initial failure explained");
        Write(file,original);
        Check(CSOL_ReloadBeamDamage() && CSOL_BeamDamageReady(),"map load from engine game directory");
        CCSOLBeamDamageConfig expected; std::string error;
        Check(expected.Parse(original,error),"reference config parsed");
        for (int v=0;v<int(CSOLBeamVariant::Count);++v)
            for (int m=0;m<int(CSOLDamageMode::Count);++m)
                for (int a=0;a<int(CSOLBeamDamage::Count);++a)
                    Check(CSOL_BeamDamage(CSOLBeamVariant(v),CSOLDamageMode(m),CSOLBeamDamage(a)) ==
                        expected.Get(CSOLBeamVariant(v),CSOLDamageMode(m),CSOLBeamDamage(a)),"runtime damage mapping");
        Write(file,Override(original,123.5));
        arguments = 2; reloadCommand();
        Check(CSOL_BeamDamage(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA) == 850,
            "unexpected command arguments cannot change path or reload");
        arguments = 1; reloadCommand();
        Check(CSOL_BeamDamage(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA) == 123.5f,
            "server command applies override");
        Check(CSOL_BeamDamage(CSOLBeamVariant::Beamgun,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA) == 850,
            "other skin unchanged");
        Write(file,"{\"version\":1,"); reloadCommand();
        Check(CSOL_BeamDamage(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA) == 123.5f,
            "interrupted save retains current values");
        Check(output.find("retaining the previous valid") != std::string::npos,"reload failure explained");
        std::filesystem::remove(file);
        Check(!CSOL_ReloadBeamDamage() && CSOL_BeamDamageReady(),"missing later map config retains valid values");
        gameDirectory.clear();
        Check(!CSOL_ReloadBeamDamage(),"empty engine directory does not load cwd file");
        gameDirectory = root.string();
        Write(file,Override(original,0)); reloadCommand();
        Check(CSOL_BeamDamage(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA) == 0,
            "zero override is active");
        Write(file,original); reloadCommand();
        Check(CSOL_BeamDamage(CSOLBeamVariant::BeamgunLE,CSOLDamageMode::Zombie,CSOLBeamDamage::ShootA) == 850,
            "removed override restores defaults");
        std::filesystem::remove(file);
        std::filesystem::remove(file.parent_path());
        std::filesystem::remove(file.parent_path().parent_path());
        std::filesystem::remove(root);
        std::cout << "PASS: " << checks << " server-adapter checks, " << sizeof(void*)*8 << "-bit process\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
