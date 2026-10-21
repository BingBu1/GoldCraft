// SPDX-License-Identifier: GPL-3.0-or-later
#include "dependencies.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc<3)throw std::runtime_error("Usage: csol_resources RESOURCE_ROOT models/model.mdl ...");
        const auto root=std::filesystem::path(argv[1]);std::set<std::string> files;
        auto exists=[&](const std::string& name){return std::filesystem::is_regular_file(root/name);};
        auto read=[&](const std::string& name)
        {
            std::ifstream file(root/name,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("missing resource: "+name);
            const auto size=file.tellg();if(size<0 || size>128*1024*1024)throw std::runtime_error("resource exceeds budget");
            std::vector<std::byte> bytes(size_t(size),std::byte{});file.seekg(0);file.read((char*)bytes.data(),size);if(!file)throw std::runtime_error("short resource read");return bytes;
        };
        for(int i=2;i<argc;++i)for(const auto& file:csol::WeaponDependencies(std::filesystem::path(argv[i]).generic_string(),read,exists))files.insert(file);
        for(const auto& file:files)std::cout<<file<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
