// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_codec.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc, char** argv)
{
    int checks = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value) throw std::runtime_error("TGA pixel mismatch");
    };
    try
    {
        for (const int bits : {24, 32})
        for (const int attributes : {0, 8})
        for (const bool rle : {false, true})
        for (const unsigned char alpha : {0, 87, 255})
        {
            std::vector<std::byte> bytes(18);
            bytes[2] = std::byte(rle ? 10 : 2);
            bytes[12] = std::byte{2}; bytes[14] = std::byte{1};
            bytes[16] = std::byte(bits); bytes[17] = std::byte(attributes);
            if (rle) bytes.push_back(std::byte{0x81});
            for (int pixel = 0; pixel < (rle ? 1 : 2); ++pixel)
            {
                bytes.insert(bytes.end(), {std::byte{9}, std::byte{121}, std::byte{226}});
                if (bits == 32) bytes.push_back(std::byte(alpha));
            }
            const auto image = csol::DecodeTGA(bytes);
            check(image.Width == 2 && image.Height == 1 && image.RGBA.size() == 8);
            for (int pixel = 0; pixel < 2; ++pixel)
            {
                check(image.RGBA[pixel * 4] == 226 && image.RGBA[pixel * 4 + 1] == 121 && image.RGBA[pixel * 4 + 2] == 9);
                check(image.RGBA[pixel * 4 + 3] == (bits == 32 ? alpha : 255));
            }
            bytes.pop_back();
            bool rejected = false;
            try { csol::DecodeTGA(bytes); } catch (const std::runtime_error&) { rejected = true; }
            check(rejected);
        }
        // Four distinct pixels make both origin bits observable, including a
        // mixed-alpha RLE raw packet rather than a uniform run.
        for (int orientation : {0, 16, 32, 48})
        for (bool rle : {false, true})
        {
            std::vector<std::byte> bytes(18);
            bytes[2]=std::byte(rle?10:2);bytes[12]=bytes[14]=std::byte{2};
            bytes[16]=std::byte{32};bytes[17]=std::byte(orientation);
            if(rle)bytes.push_back(std::byte{3});
            for(int i=0;i<4;++i)bytes.insert(bytes.end(),{std::byte(10+i),std::byte(20+i),std::byte(30+i),std::byte(i*85)});
            const auto image=csol::DecodeTGA(bytes);
            for(int y=0;y<2;++y)for(int x=0;x<2;++x)
            {
                const int source=(orientation&32?y:1-y)*2+(orientation&16?1-x:x);
                const int target=(y*2+x)*4;
                check(image.RGBA[target]==30+source&&image.RGBA[target+1]==20+source&&
                    image.RGBA[target+2]==10+source&&image.RGBA[target+3]==source*85);
            }
        }
        // Optional original uncompressed CSO images: compare every channel to
        // their on-disk BGRA bytes, independently of descriptor alpha flags.
        for (int arg = 1; arg < argc; ++arg)
        {
            std::ifstream file(argv[arg], std::ios::binary);
            if (!file) throw std::runtime_error("image fixture missing");
            const std::vector<unsigned char> raw{std::istreambuf_iterator<char>(file), {}};
            check(raw.size() >= 18 && raw[1] == 0 && raw[2] == 2 && raw[16] == 32);
            const auto image = csol::DecodeTGA({reinterpret_cast<const std::byte*>(raw.data()), raw.size()});
            const auto offset = 18u + raw[0];
            check(raw.size() >= offset + image.RGBA.size());
            for (unsigned y = 0; y < image.Height; ++y)
            for (unsigned x = 0; x < image.Width; ++x)
            {
                const auto sx = raw[17] & 16 ? image.Width - 1 - x : x;
                const auto sy = raw[17] & 32 ? y : image.Height - 1 - y;
                const auto source = offset + (sy * image.Width + sx) * 4;
                const auto target = (y * image.Width + x) * 4;
                check(image.RGBA[target] == raw[source + 2] && image.RGBA[target + 1] == raw[source + 1] &&
                    image.RGBA[target + 2] == raw[source] && image.RGBA[target + 3] == raw[source + 3]);
            }
        }
        std::cout << "PASS: " << checks << " TGA pixel/alpha checks\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
