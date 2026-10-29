// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_damage_file.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
unsigned checks = 0;
void Check(bool valid, const char* message)
{
    ++checks;
    if (!valid) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    Check(bool(file), "cannot write fixture file");
}
float Damage(const CCSOLBeamDamageConfig& config)
{
    return config.Get(CSOLBeamVariant::BeamgunLE, CSOLDamageMode::Zombie, CSOLBeamDamage::ShootA);
}
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 3, "sample and dedicated fixture directory required");
        std::ifstream sample(argv[1], std::ios::binary);
        Check(bool(sample), "sample missing");
        const std::string original{std::istreambuf_iterator<char>(sample), {}};
        const auto directory = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(directory);
        // Non-ASCII and spaces exercise the native filesystem path overload.
        const auto path = directory / std::filesystem::path(L"伤害 参数.json");
        const auto missing = directory / "absent.json";
        Check(!std::filesystem::exists(missing), "missing-file fixture unexpectedly exists");
        CCSOLBeamDamageConfig config;
        std::string error;
        Check(!CSOL_LoadBeamDamageFile(missing, config, error) && !error.empty(), "first missing file accepted");
        Check(!config.IsLoaded(), "first failure activated config");
        Check(!CSOL_LoadBeamDamageFile(directory, config, error), "directory accepted as config");

        Write(path, original);
        Check(CSOL_LoadBeamDamageFile(path, config, error) && error.empty(), "valid file rejected");
        Check(config.IsLoaded() && Damage(config) == 850, "wrong original damage");
        auto changed = original;
        const auto start = changed.find("\"beamgunle\": {}");
        Check(start != changed.npos, "expected empty sample override missing");
        changed.replace(start, std::string("\"beamgunle\": {}").size(),
            "\"beamgunle\": {\"ShootADamageZombie\": 123.5}");
        Write(path, changed);
        Check(CSOL_LoadBeamDamageFile(path, config, error), "reload failed");
        Check(Damage(config) == 123.5f, "reload did not replace damage");
        Check(config.Get(CSOLBeamVariant::Beamgun, CSOLDamageMode::Zombie, CSOLBeamDamage::ShootA) == 850,
            "reload contaminated other weapon");

        const auto reject = [&](const std::string& bytes)
        {
            Write(path, bytes);
            Check(!CSOL_LoadBeamDamageFile(path, config, error) && !error.empty(), "invalid file accepted");
            Check(config.IsLoaded() && Damage(config) == 123.5f, "failed read lost active config");
        };
        reject("");
        reject(changed.substr(0, changed.size() / 2));
        reject(changed + std::string(1, '\0'));
        reject(std::string(65537, ' '));
        reject(std::string(1024 * 1024, ' '));
        Check(!CSOL_LoadBeamDamageFile(missing, config, error) && Damage(config) == 123.5f,
            "missing reload lost active config");

        Write(path, changed + std::string(65536 - changed.size(), ' '));
        Check(CSOL_LoadBeamDamageFile(path, config, error), "exact maximum file rejected");
        Check(Damage(config) == 123.5f, "maximum-size file changed damage");
        Write(path, "\xEF\xBB\xBF" + original);
        Check(CSOL_LoadBeamDamageFile(path, config, error) && error.empty(), "UTF-8 BOM rejected");
        Check(Damage(config) == 850, "removed override remained after reload");

#ifdef _WIN32
        // Windows denies opening while another editor holds an exclusive
        // handle. Failure must preserve the active snapshot without fallback.
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(locked != INVALID_HANDLE_VALUE, "exclusive fixture handle failed");
        const bool loadedWhileLocked = CSOL_LoadBeamDamageFile(path, config, error);
        CloseHandle(locked);
        Check(!loadedWhileLocked && !error.empty(), "exclusive lock was ignored");
        Check(config.IsLoaded() && Damage(config) == 850, "exclusive lock lost active config");
        Check(CSOL_LoadBeamDamageFile(path, config, error) && error.empty(), "load failed after closing handle");
#endif
        // Exact files only; never recursively delete an input directory.
        Check(std::filesystem::remove(path), "fixture file cleanup failed");
        Check(!CSOL_LoadBeamDamageFile(path, config, error) && Damage(config) == 850,
            "deleted config did not retain snapshot");
        std::cout << checks << " file-loading assertions passed; " << sizeof(void*) * 8 << "-bit process\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
