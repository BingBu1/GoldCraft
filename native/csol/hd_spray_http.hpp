// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hd_spray.hpp"
#include <charconv>

namespace csol::spray::http {
struct Endpoint { std::string host;unsigned short port{};bool secure{}; };
inline Endpoint ParseEndpoint(std::string_view url){
    Check(url.size()<=128);const bool secure=url.starts_with("https://");const size_t start=secure?8:7;
    Check(secure||url.starts_with("http://"));const auto slash=url.find('/',start);
    Check(slash!=std::string_view::npos&&url.substr(slash)=="/spray/");
    const auto authority=url.substr(start,slash-start);const auto colon=authority.find(':');
    const auto host=authority.substr(0,colon);Check(!host.empty()&&host.front()!='.'&&host.back()!='.'&&host.find("..") == std::string_view::npos);
    for(unsigned char c:host)Check((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-');
    unsigned port=secure?443:80;if(colon!=std::string_view::npos){const auto text=authority.substr(colon+1);const auto r=std::from_chars(text.data(),text.data()+text.size(),port);Check(r.ec==std::errc{}&&r.ptr==text.data()+text.size()&&port>0&&port<=65535);}
    return {std::string(host),static_cast<unsigned short>(port),secure};
}
inline std::string SharedEndpoint(std::string_view url){
    if(url.empty())return {};
    const auto path=url.find('/',url.starts_with("https://")?8:7);Check(path!=std::string_view::npos&&url.substr(path)=="/goldcraft/");
    const auto result=std::string(url.substr(0,path))+"/spray/";ParseEndpoint(result);return result;
}
inline std::string ReadString(Reader& r){const auto length=r.U8();Check(length<=128&&length<=r.bytes.size()-r.at);const auto* data=reinterpret_cast<const char*>(r.bytes.data()+r.at);r.at+=length;return {data,length};}
inline void PutString(Bytes& bytes,const std::string& value){Check(value.size()<=128);bytes.push_back(std::uint8_t(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());}
}
