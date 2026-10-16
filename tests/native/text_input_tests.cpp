#include "goldcraft/text_input.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace goldcraft::text_input;
int main(){
    std::vector<std::uint32_t> chars;
    auto decode_text=[&](std::string_view text){return decode(text,[&](auto cp){chars.push_back(cp);});};
    assert(decode_text("中文@2+./0123456789"));
    assert(chars[0]==0x4e2d&&chars[1]==0x6587&&chars[2]=='@'&&chars.back()=='9');
    chars.clear();assert(decode_text("\xf0\x9f\x98\x80")&&chars==std::vector<std::uint32_t>{0x1f600});
    for(const std::string text:{"ok\xc0\xaf","ok\xed\xa0\x80","ok\xf4\x90\x80\x80","ok\xe4\xb8","ok\x80"}){
        chars.clear();assert(!decode_text(text)&&chars.empty());
    }
    chars.clear();assert(decode_text("\n\r\t\x7f")&&chars.empty());
    assert(wide("中文")==L"中文");assert(wide("\xf0\x9f\x98\x80").size()==2);
    Session s;assert(s.set({11,21,31},100));
    assert(s.accepts(31,100)&&!s.accepts(32,100)&&!s.accepts(31,99));
    assert(!s.set({11,21,31},110)&&s.since==100);
    s.editing(true);assert(s.block_key(257)&&s.block_key(256)&&s.block_key(259)&&!s.block_key(340));
    s.end_batch();assert(s.block_key(257));
    // SDL clears preedit before committing; protect Enter for the whole batch.
    s.editing(false);s.committed();assert(s.block_key(257));
    s.end_batch();assert(!s.block_key(257));
    s.editing(true);assert(s.set({11,22,31},200)&&!s.block_key(257)&&!s.accepts(31,150));
    assert(s.set({},210)&&!s.accepts(31,211));
    s.set({12,22,31},0xfffffffffffffff0);assert(s.accepts(31,5)&&!s.accepts(31,0xffffffffffffff00));
    std::cout<<"PASS committed UTF-8, Chinese/symbol/keypad text, atomic rejection, Unicode scalars, target isolation and IME selection lifecycle\n";
}
