// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequences.hpp"
#include "model_codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
static std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("cannot open model/sequence");
    const auto length=file.tellg();if(length<0||length>128*1024*1024)throw std::runtime_error("input size outside budget");
    std::vector<std::byte> data(size_t(length),std::byte{});file.seekg(0);file.read((char*)data.data(),length);
    if(!file)throw std::runtime_error("short input read");return data;
}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        const bool extended=argc>1 && std::wstring_view(argv[1])==L"--extended";
        const int first=extended?2:1;
        if(argc<first+2)throw std::runtime_error("usage: csol_sequence_pack [--extended] OUTPUT NEW_BASE_MODEL [SEQUENCE...]");
        const std::filesystem::path output=argv[first];if(std::filesystem::exists(output))throw std::runtime_error("output already exists");
        auto base=Read(argv[first+1]);csol::DecodeStudioModel(base);
        std::vector<std::vector<std::byte>> sequences;
        for(int i=first+2;i<argc;++i)sequences.push_back(Read(argv[i]));
        const auto merged=csol::AppendSequences(base,sequences);
        int count,index;std::memcpy(&count,merged.data()+164,4);std::memcpy(&index,merged.data()+168,4);
        if(count>(extended?2048:256))throw std::runtime_error("player sequences exceed selected network format");
        std::filesystem::create_directories(output.parent_path());
        std::ofstream file(output,std::ios::binary);file.write((const char*)merged.data(),merged.size());
        if(!file)throw std::runtime_error("output write failed");
        std::cout<<"sequences="<<count<<" bytes="<<merged.size()<<'\n';
        for(int i=0;i<count;++i){const char* name=(const char*)merged.data()+index+i*176;
            if(std::string_view(name,strnlen(name,32)).find("skull8")!=std::string_view::npos)std::cout<<i<<" "<<std::string_view(name,strnlen(name,32))<<'\n';}
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
