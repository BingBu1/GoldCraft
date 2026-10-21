// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_codec.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace csol {
namespace {
[[noreturn]] void Invalid(const char* reason) { throw std::runtime_error(reason); }
struct Reader
{
	std::span<const std::byte> Data;
	std::size_t Offset = 0;
	const unsigned char* Take(std::size_t count)
	{
		if (count > Data.size() - Offset) Invalid("资源数据不完整");
		const auto* p = reinterpret_cast<const unsigned char*>(Data.data() + Offset);
		Offset += count;
		return p;
	}
};
unsigned U16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
void Dimensions(unsigned w, unsigned h)
{
	if (!w || !h || w > 4096 || h > 4096) Invalid("贴图尺寸超出范围（1–4096）");
}
using Color = std::array<unsigned char, 4>;
}
Image DecodeBMP(std::span<const std::byte> bytes)
{
    const auto u16=[&](size_t p)->unsigned { if(p+2>bytes.size())Invalid("BMP header truncated"); return unsigned(bytes[p])|(unsigned(bytes[p+1])<<8); };
    const auto u32=[&](size_t p)->uint32_t { return u16(p)|(uint32_t(u16(p+2))<<16); };
    if(bytes.size()<54 || u16(0)!=0x4d42 || u32(14)<40 || u16(26)!=1 || u32(30)!=0)Invalid("unsupported BMP header");
    const auto at=u32(10),dib=u32(14); const int w=int32_t(u32(18)),h=int32_t(u32(22)); const auto bits=u16(28);
    if(w<1 || w>4096 || !h || h<-4096 || h>4096 || (bits!=8 && bits!=24 && bits!=32))Invalid("invalid BMP dimensions/format");
    const unsigned height=h<0?-h:h, stride=(w*bits+31)/32*4;
    if(at>bytes.size() || size_t(stride)*height>bytes.size()-at || dib>bytes.size()-14 || at<14+dib)Invalid("BMP pixels truncated");
    const auto colors=bits==8?(u32(46)?u32(46):256):0;
    if(colors>256 || size_t(colors)*4>at-14-dib)Invalid("BMP palette truncated");
    Image image{unsigned(w),height,std::vector<uint8_t>(size_t(w)*height*4)};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<unsigned(w);++x)
    {
        size_t p=at+(h<0?y:height-1-y)*stride+x*(bits/8);
        if(bits==8){const auto index=unsigned(bytes[p]);if(index>=colors)Invalid("BMP palette index out of bounds");p=14+dib+index*4;}
        auto* out=image.RGBA.data()+(y*w+x)*4;
        out[0]=uint8_t(bytes[p+2]);out[1]=uint8_t(bytes[p+1]);out[2]=uint8_t(bytes[p]);out[3]=255;
    }
    return image;
}

Image DecodeTGA(std::span<const std::byte> bytes)
{
	Reader reader{bytes};
	const auto* h = reader.Take(18);
	const unsigned w = U16(h + 12), height = U16(h + 14);
	Dimensions(w, height);
	const int type = h[2] & 7;
	const bool rle = (h[2] & 8) != 0;
	if ((h[2] != 1 && h[2] != 2 && h[2] != 3 && h[2] != 9 && h[2] != 10 && h[2] != 11)
		|| (h[17] & 0xC0) || (type == 1 && (h[1] != 1 || h[16] != 8))
		|| (type == 2 && h[16] != 24 && h[16] != 32) || (type == 3 && h[16] != 8))
		Invalid("不支持此 TGA 编码");
	reader.Take(h[0]);
	std::vector<Color> palette(256, {0, 0, 0, 255});
	const unsigned first = U16(h + 3), length = U16(h + 5);
	if (h[1])
	{
		if (first > 255 || length > 256 - first || (h[7] != 24 && h[7] != 32)) Invalid("无效 TGA 调色板");
		for (unsigned i = first; i < first + length; ++i)
		{
			const auto* p = reader.Take(h[7] / 8);
			palette[i] = {p[2], p[1], p[0], static_cast<unsigned char>(h[7] == 32 ? p[3] : 255)};
		}
	}
	auto pixel = [&]() -> Color
	{
		const auto* p = reader.Take(h[16] / 8);
		if (type == 1)
		{
			if (p[0] < first || p[0] >= first + length) Invalid("TGA 调色板索引越界");
			return palette[p[0]];
		}
		if (type == 3) return {p[0], p[0], p[0], 255};
		return {p[2], p[1], p[0], static_cast<unsigned char>(h[16] == 32 && (h[17] & 15) ? p[3] : 255)};
	};
	Image image{w, height, std::vector<std::uint8_t>(std::size_t(w) * height * 4)};
	unsigned index = 0;
	while (index < w * height)
	{
		const unsigned packet = rle ? *reader.Take(1) : 0;
		const unsigned run = rle ? (packet & 127) + 1 : 1;
		if (run > w * height - index) Invalid("TGA RLE 数据越界");
		Color repeated{};
		if (packet & 128) repeated = pixel();
		for (unsigned i = 0; i < run; ++i, ++index)
		{
			const auto p = (packet & 128) ? repeated : pixel();
			const auto x = h[17] & 16 ? w - 1 - index % w : index % w;
			const auto y = h[17] & 32 ? index / w : height - 1 - index / w;
			std::memcpy(image.RGBA.data() + y * w * 4 + x * 4, p.data(), 4);
		}
	}
	return image;
}

}
