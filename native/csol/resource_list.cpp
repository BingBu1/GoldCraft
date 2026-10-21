// SPDX-License-Identifier: GPL-3.0-or-later
#include "dependencies.hpp"
#include "weapon_resources.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc<3)throw std::runtime_error("Usage: csol_resources RESOURCE_ROOT models/model.mdl ...");
        const auto root=std::filesystem::path(argv[1]);std::set<std::string> files;
        const bool json=std::wstring_view(argv[2])==L"--json";if(json && argc<4)throw std::runtime_error("no models supplied");
        auto exists=[&](const std::string& name){return std::filesystem::is_regular_file(root/csol::WeaponResourceSource(name));};
        auto read=[&](const std::string& name)
        {
            std::ifstream file(root/csol::WeaponResourceSource(name),std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("missing resource: "+name);
            const auto size=file.tellg();if(size<0 || size>128*1024*1024)throw std::runtime_error("resource exceeds budget");
            std::vector<std::byte> bytes(size_t(size),std::byte{});file.seekg(0);file.read((char*)bytes.data(),size);if(!file)throw std::runtime_error("short resource read");return bytes;
        };
        std::vector<csol::ModelRedirect> configured;
        std::vector<std::filesystem::path> configurations;const auto directory=root/"weapons";
        if(std::filesystem::is_directory(directory))for(const auto& entry:std::filesystem::directory_iterator(directory))
            if(entry.is_regular_file() && entry.path().extension()==".json")configurations.push_back(entry.path());
        std::sort(configurations.begin(),configurations.end());
        for(const auto& path:configurations)
        {
            const auto name="weapons/"+path.filename().string();const auto bytes=read(name);
            try{auto entries=csol::ParseWeaponRedirects({(const char*)bytes.data(),bytes.size()},name);configured.insert(configured.end(),entries.begin(),entries.end());}
            catch(const std::exception& error){std::cerr<<"Skipping "<<name<<": "<<error.what()<<'\n';}
        }
        for(int i=json?3:2;i<argc;++i)for(const auto& file:csol::WeaponDependencies(std::filesystem::path(argv[i]).generic_string(),read,exists,configured))files.insert(file);
        if(!json){for(const auto& file:files)std::cout<<file<<'\n';return 0;}
        rapidjson::StringBuffer buffer;rapidjson::Writer writer(buffer);writer.StartArray();
        auto field=[&](const char* key,const std::string& value){writer.Key(key);writer.String(value.c_str(),rapidjson::SizeType(value.size()));};
        for(const auto& file:files)
        {
            csol::ModelSource source{file,file,file};if(std::string_view(file).ends_with(".mdl"))source=csol::ResolveModelSource(file,read,exists,configured);
            writer.StartObject();field("path",file);
            if(file=="sprites/weapon_tknifeex2.txt")field("content",std::string(csol::TKnifeEx2HUD));
            else field("source",std::string(csol::WeaponResourceSource(source.File)));
            writer.EndObject();
            if(source.Redirected)
            {
                writer.StartObject();field("path",csol::ModelSourceDescriptor(file));field("content",csol::SerializeModelSource(source));writer.EndObject();
            }
        }
        writer.EndArray();std::cout<<buffer.GetString()<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
