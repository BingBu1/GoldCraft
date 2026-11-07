// SPDX-License-Identifier: GPL-3.0-or-later
// Link the actual localization implementation, then read the unchanged full
// catalog. --catalog-only also permits an exact old-object regression control.
#include "native/csol/scenario/core/scenario_localization.hpp"
#include "localization_catalog_expectations.hpp"
#include <algorithm>
#include <windows.h>
#include <bcrypt.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <span>
#include <stdexcept>

namespace
{
namespace hud=csol::scenario_hud;
unsigned checks{},failures{};
void Check(bool ok,const char* label,std::source_location at=std::source_location::current())
{++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<" line="<<at.line()<<'\n';}}
void Valid(std::u16string_view source,const hud::LocalizedTokens& expected,const char* label)
{
    std::set<std::u16string> wanted;
    for(const auto& [key,value]:expected)wanted.insert(key);
    hud::LocalizedTokens actual;std::string error;
    const bool parsed=hud::ReadLocalizedTokens(source,wanted,actual,error);
    Check(parsed&&error.empty(),label);
    Check(actual==expected,"exact keys and values retained");
}
void Invalid(std::u16string_view source,const char* label)
{
    const hud::LocalizedTokens previous{{u"saved",u"prior value"}};
    auto actual=previous;std::string error;
    Check(!hud::ReadLocalizedTokens(source,{u"Wanted"},actual,error)&&!error.empty(),label);
    Check(actual==previous,"parse failure preserves prior catalog transactionally");
}
void SeparatorBoundaries()
{
    Valid(u"\"Tokens\" {\n===\n\"Before\" \"one\"\n\t ===== \t\r\n\"After\" \"two\"\n}",
        {{u"Before",u"one"},{u"After",u"two"}},"standalone bare separators preserve keys before and after");
    Valid(u"\"Tokens\" {\n\"===\"\n\"quoted key\"\n}",
        {{u"===",u"quoted key"}},"quoted equals key on its own line is not skipped");
    Valid(u"\"Tokens\" {\n=== value\n}",
        {{u"===",u"value"}},"inline bare equals key with value is not skipped");
    Valid(u"\"Tokens\" {\n\"QuotedValue\" \"===\"\n\"InlineValue\" ===\n\"NextLineValue\"\n====\n}",
        {{u"QuotedValue",u"==="},{u"InlineValue",u"==="},{u"NextLineValue",u"===="}},
        "quoted, inline and standalone equals values remain values");
    Valid(u"\"Tokens\" {\n=\n\"single key\"\n==\n\"double key\"\n}",
        {{u"=",u"single key"},{u"==",u"double key"}},"single and double equals keys are not separators");
    Valid(u"\"Tokens\" { === \"inline after opening brace\"\n}",
        {{u"===",u"inline after opening brace"}},"non-whitespace before equals on physical line prevents skipping");
    Invalid(u"\"Tokens\" {\n===\n\"Wanted\"\n}","key after separator still requires a value");
    Invalid(u"\"Tokens\" {\n===\n\"Wanted\"", "separator before trailing key cannot become successful empty text");
    Invalid(u"\"Tokens\" {\n===\n\"Wanted\" \"unfinished", "half-quoted value after separator is rejected");
    Invalid(u"\"Tokens\" {\n===\n\"unfinished", "half-quoted key after separator is rejected");
    Invalid(u"\"Tokens\" {\n===\n\"Wanted\" {\n}", "brace cannot become missing string value");
    Invalid(u"\"Tokens\" {\n===\n\"Wanted\" \"value\" [$WIN32]\n}", "unsupported conditional remains rejected");
    Invalid(u"\"Tokens\" {\n===\n\"Wanted\" \"value\"", "separator does not hide unclosed token block");
}
void CatalogBoundaries()
{
    for(const std::u16string annotation:{u"---",u"Ver.283",u"\uf142"})
    {
        Valid(u"\"Tokens\" {\n\"Before\" \"one\"\n\u2003"+annotation+u"\t\n\"After\" \"two\"\n}",
            {{u"Before",u"one"},{u"After",u"two"}},"confirmed standalone key annotations preserve adjacent values");
        Valid(u"\"Tokens\" {\n\""+annotation+u"\"\n\"quoted key\"\n}",
            {{annotation,u"quoted key"}},"quoted annotation remains a literal key");
        Valid(u"\"Tokens\" {\n"+annotation+u" inline\n}",
            {{annotation,u"inline"}},"inline annotation is an ordinary key");
        Valid(u"\"Tokens\" {\n\"Wanted\"\n"+annotation+u"\n}",
            {{u"Wanted",annotation}},"standalone annotation in value position remains data");
        Invalid(u"\"Tokens\" {\n"+annotation+u"\n\"Wanted\"\n}","annotation cannot suppress following missing value");
    }
    Valid(u"\"Tokens\" {\n- one\n-- two\nVer. bare\nVer.2x suffix\nVer.-1 sign\nver.283 lowercase\n\uf143 other\n}",
        {{u"-",u"one"},{u"--",u"two"},{u"Ver.",u"bare"},{u"Ver.2x",u"suffix"},
         {u"Ver.-1",u"sign"},{u"ver.283",u"lowercase"},{u"\uf143",u"other"}},
        "nearby unknown annotation forms stay ordinary key data");
    Invalid(u"\"Tokens\" {\nUnknownDecoration\n}","unknown standalone key still requires a value");
    Valid(u"\"Tokens\" {\n\"A\"\u2003\"one\"\u2003\n\u2003\"B\" \"two\"\n}",
        {{u"A",u"one"},{u"B",u"two"}},"EM SPACE outside strings separates tokens and never shifts following entries");
    Valid(u"\"Tokens\" {\"A\" \"one\u2003two\"}",{{u"A",u"one\u2003two"}},"quoted EM SPACE is preserved");
    Valid(u"\"Tokens\" {\n\"A\",\"one, two\"\n\"B\" , \"line\\nnext\"\n}",
        {{u"A",u"one, two"},{u"B",u"line\nnext"}},"single comma between quoted strings decodes complete values");
    Valid(u"\"Tokens\" {\"A\",\"\"}",{{u"A",u""}},"explicit quoted empty comma value remains valid");
    Valid(u"\"Tokens\" {\",\" \"literal key\" \"Value\" \",\"}",
        {{u",",u"literal key"},{u"Value",u","}},"quoted commas remain literal data");
    Valid(u"\"Tokens\" {\n\"A\" \"first\r\n===\r\n---\r\nVer.283\r\n\uf142\r\nlast\"\n}",
        {{u"A",u"first\r\n===\r\n---\r\nVer.283\r\n\uf142\r\nlast"}},"physical multiline values keep annotation-looking lines");
    Valid(u"\"Tokens\" {\"[english]\r\nName\" \"value\"}",
        {{u"[english]\r\nName",u"value"}},"physical multiline quoted keys remain exact");
    Valid(u"\"Tokens\" {\"A\" \"first\r\nline\" \"A\" \"last\r\nline\"}",
        {{u"A",u"last\r\nline"}},"later duplicate multiline definition wins");
    for(const auto malformed:{u"\"Tokens\" {\"Wanted\",}",u"\"Tokens\" {\"Wanted\",",
        u"\"Tokens\" {\"Wanted\",,\"value\"}",u"\"Tokens\" {\"Wanted\",bare}",
        u"\"Tokens\" {Wanted,\"value\"}",u"\"Tokens\" {,\"value\"}",
        u"\"Tokens\" {\"Wanted\",\"value\",}",u"\"Tokens\" {\"Wanted\",\"unfinished",
        u"\"Tokens\" {\"Wanted\",\"value\" [$WIN32]}",u"\"Tokens\" {\"Wanted\",\nVer.283\n}",
        u"\"Tokens\" {\"Wanted\",\n===\n}"})
        Invalid(malformed,"bad comma, unquoted value, missing value or conditional remains transactional failure");
}
std::u16string ReadCatalog(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("full catalog missing");
    const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    if(bytes.size()<2||bytes.size()%2||static_cast<unsigned char>(bytes[0])!=0xff||
        static_cast<unsigned char>(bytes[1])!=0xfe)throw std::runtime_error("full catalog must be UTF16LE");
    std::u16string text;text.reserve(bytes.size()/2-1);
    for(std::size_t at=2;at<bytes.size();at+=2)
        text.push_back(char16_t(static_cast<unsigned char>(bytes[at])|(unsigned(static_cast<unsigned char>(bytes[at+1]))<<8)));
    while(!text.empty()&&text.back()==u'\0')text.pop_back();
    return text;
}
std::string HashValue(std::u16string_view value)
{
    BCRYPT_ALG_HANDLE algorithm{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("SHA256 provider unavailable");
    std::array<unsigned char,32> digest{};
    const auto status=BCryptHash(algorithm,nullptr,0,
        reinterpret_cast<PUCHAR>(const_cast<char16_t*>(value.data())),ULONG(value.size()*sizeof(char16_t)),
        digest.data(),ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0)throw std::runtime_error("SHA256 calculation failed");
    constexpr char digits[]="0123456789abcdef";
    std::string result;result.reserve(64);
    for(auto byte:digest){result+=digits[byte>>4];result+=digits[byte&15];}
    return result;
}
void FullCatalog(const std::filesystem::path& path,bool extended)
{
    hud::LocalizedTokens actual;std::string error;
    std::set<std::u16string> wanted{u"CSO_ZBS_UseRetryItem",u"CSO_ZBS_RetryCountText",u"CSO_ZBS_RetryCountTextunlimit"};
    if(extended)
        for(const auto values:{std::span<const localization_expectations::Value>(localization_expectations::Multiline),
                              std::span<const localization_expectations::Value>(localization_expectations::CommaValues)})
            for(const auto& value:values)wanted.insert(std::u16string(value.key));
    const bool parsed=hud::ReadLocalizedTokens(ReadCatalog(path),wanted,actual,error);
    const auto before=actual.find(u"CSO_ZBS_UseRetryItem"),title=actual.find(u"CSO_ZBS_RetryCountText"),
        unlimited=actual.find(u"CSO_ZBS_RetryCountTextunlimit");
    if(!parsed)std::cerr<<"catalog error="<<error<<'\n';
    Check(parsed&&error.empty(),"actual full catalog accepted by production parser");
    Check(before!=actual.end()&&before->second==u"按'R'键使用回合重置，可以重新挑战当前关卡。",
        "full catalog keeps original retry token before separator");
    Check(title!=actual.end()&&title->second==u"回合重置可使用次数","full catalog keeps budget title after separator");
    Check(unlimited!=actual.end()&&unlimited->second==u"：无限制","full catalog keeps unlimited token after separator");
    std::cout<<"catalog parse="<<parsed<<" before="<<(before!=actual.end())<<" title="<<(title!=actual.end())
        <<" unlimited="<<(unlimited!=actual.end())<<'\n';
    if(extended)
    {
        for(const auto values:{std::span<const localization_expectations::Value>(localization_expectations::Multiline),
                              std::span<const localization_expectations::Value>(localization_expectations::CommaValues)})
            for(const auto& expected:values)
            {
                const auto found=actual.find(expected.key);
                const bool valid=found!=actual.end()&&found->second.size()==expected.units&&
                    std::count(found->second.begin(),found->second.end(),u'\r')==expected.carriageReturns&&
                    std::count(found->second.begin(),found->second.end(),u'\n')==expected.lineFeeds&&
                    HashValue(found->second)==expected.sha256;
                Check(valid,"full-catalog multiline or comma value matches frozen UTF16LE hash and newline counts");
                if(!valid)std::cerr<<"catalog value key="<<std::string(expected.key.begin(),expected.key.end())<<'\n';
            }
        std::cout<<"catalog semantic keys=140 commaValues=9\n";
    }
}
}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc!=2&&argc!=3)throw std::runtime_error("expected full catalog path and optional prior catalog or --catalog-only");
        const bool control=argc==3&&std::wstring_view(argv[2])==L"--catalog-only";
        if(!control){SeparatorBoundaries();CatalogBoundaries();}
        FullCatalog(argv[1],!control);
        if(argc==3&&!control)FullCatalog(argv[2],true);
        std::cout<<"scenario_localization checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
