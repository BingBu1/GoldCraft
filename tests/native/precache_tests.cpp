#include "goldcraft/precache_protocol.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace goldcraft::precache;
static int checks;
static void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}

int main() {
    try {
        struct Case { std::uint32_t index; unsigned length; std::array<std::uint8_t, 6> bytes; };
        const Case cases[] = {
            {0, 2, {0, 0}}, {511, 2, {255, 1}}, {512, 2, {0, 2}},
            {32768, 2, {0, 128}}, {65534, 2, {254, 255}},
            {65535, 6, {255, 255, 255, 255, 0, 0}},
            {65536, 6, {255, 255, 0, 0, 1, 0}},
            {16777217, 6, {255, 255, 1, 0, 0, 1}},
            {2147483647, 6, {255, 255, 255, 255, 255, 127}},
        };
        for (const auto& test : cases) {
            std::array<std::uint8_t, 7> bytes{}; bytes.back() = 0x5a;
            check(encode_media(bytes.data(), test.index) == test.length, "wire length");
            check(std::equal(bytes.begin(), bytes.begin() + test.length, test.bytes.begin()), "exact wire bytes");
            check(bytes.back() == 0x5a, "adjacent field preserved");
            check(decode_media(test.bytes.data()) == test.index, "decode independently specified bytes");
            // Both the engine and CS user-message parser return a signed
            // short. Exercise the production reader with bounded callbacks.
            for (unsigned available = 0; available <= test.length; ++available) {
                unsigned cursor = 0;
                bool bad = false;
                auto read = [&](unsigned width) {
                    if (width > available - cursor) { bad = true; return -1; }
                    std::uint32_t value = 0;
                    for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(test.bytes[cursor++]) << (8 * i);
                    return width == 2 ? int(static_cast<std::int16_t>(value)) : int(value);
                };
                std::uint32_t value = 0;
                const bool valid = read_media_index([&] { return read(2); }, [&] { return read(4); }, [&] { return bad; }, true, value);
                check(valid == (available == test.length), "truncated media fails before another message field");
                if (valid) check(value == test.index && cursor == test.length, "signed client reader restores full ID");
            }
        }
        for (unsigned offset = 0; offset < 32; ++offset) {
            check(user_message_media_field(UserMediaMessage::brass_cs, offset) == (offset == 20), "CS Brass model alone is expanded");
            check(user_message_media_field(UserMediaMessage::brass_cz, offset) == (offset == 13), "CZ Brass model alone is expanded");
            check(!user_message_media_field(UserMediaMessage::none, offset), "other user messages retain their schema");
        }
        std::uint32_t invalid = 0;
        check(!read_media_index([] { return -1; }, [] { return -1; }, [] { return false; }, true, invalid), "negative 32-bit media rejected");
        check(read_media_index([] { return -1; }, [] { throw std::runtime_error("legacy overread"); return 0; },
            [] { return false; }, false, invalid) && invalid == 65535, "unextended escape remains a legacy short");
        // Consecutive high indices must not shift the second media field's
        // schema offset or consume the following color/scale bytes.
        const std::array<std::uint8_t, 15> blood{
            115, 255,255,255,255,0,0, 255,255,0,0,1,0, 70,8};
        check(decode_media(blood.data()+1) == 65535 && decode_media(blood.data()+7) == 65536, "two escaped sprite IDs");
        check(temp_model_field(115,7) && temp_model_field(115,9), "blood sprite fields");
        check(!temp_model_field(115,11), "blood color is not widened");
        check(temp_model_field(121,3) && !temp_model_field(121,2), "actual HL10210 player-sprites schema");
        check(temp_model_field(123,9) && !temp_model_field(123,7), "firefield radius stays short");
        check(temp_model_field(13,11) && !temp_model_field(13,7) && !temp_model_field(13,9), "BSP decal/entity stay short");
        check(!temp_model_field(112,11), "player decal has no model field in HL10210");
        check(message_media_field(29,0,6) && !message_media_field(29,0,10), "static sound entity stays short");
        check(!message_media_field(64,115,7), "user message shorts remain untouched");
        check(ManifestChunk{5000,0,128}.valid(0), "large batched manifest");
        check(!ManifestChunk{5000,128,128}.valid(0), "out-of-order manifest rejected");
        check(!ManifestChunk{5000,4999,2}.valid(4999), "last batch overflow rejected");
        check(!ManifestChunk{5000,0,129}.valid(0), "oversized batch rejected");
        check(!ManifestChunk{0xffffffff,0,1}.valid(0), "negative API total rejected");
        check(ManifestChunk{65537,65536,1}.valid(65536) && ManifestChunk{65537,65536,1}.last(), "32-bit manifest ordinal");
        std::cout << checks << " precache wire checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
