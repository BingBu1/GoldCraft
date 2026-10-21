// Derived from the verified HLAM CSOLFormats DDS decoder in this project.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sprite_codec.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace csol {
namespace {
[[noreturn]] void Invalid(const char* message) { throw std::runtime_error(message); }
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
unsigned U32(const unsigned char* p) { return U16(p) | (U16(p + 2) << 16); }
void Dimensions(unsigned w, unsigned h)
{
	if (!w || !h || w > 4096 || h > 4096) Invalid("贴图尺寸超出范围（1–4096）");
}
using Color = std::array<unsigned char, 4>;
Color RGB565(unsigned v)
{
	const unsigned r = v >> 11, g = (v >> 5) & 63, b = v & 31;
	return {static_cast<unsigned char>((r << 3) | (r >> 2)),
		static_cast<unsigned char>((g << 2) | (g >> 4)),
		static_cast<unsigned char>((b << 3) | (b >> 2)), 255};
}

SpriteFrame DDS(Reader& reader, unsigned expectedW, unsigned expectedH, bool pixels)
{
	const auto* h = reader.Take(128);
	if (std::memcmp(h, "DDS ", 4) || U32(h + 4) != 124 || U32(h + 76) != 32)
		Invalid("SPR v3 帧缺少有效的 DDS 头");
	const unsigned w = U32(h + 16), height = U32(h + 12);
	Dimensions(w, height);
	if (w != expectedW || height != expectedH) Invalid("SPR 与 DDS 帧尺寸不一致");
	if ((U32(h + 80) & 4) == 0 || U32(h + 112) != 0 || U32(h + 24) > 1)
		Invalid("暂不支持此 DDS 像素格式、立方体或体积贴图");
	const bool dxt1 = std::memcmp(h + 84, "DXT1", 4) == 0;
	const bool dxt3 = std::memcmp(h + 84, "DXT3", 4) == 0;
	const bool dxt5 = std::memcmp(h + 84, "DXT5", 4) == 0;
	if (!dxt1 && !dxt3 && !dxt5) Invalid("SPR v3 目前支持 DXT1、DXT3、DXT5");
	const unsigned blockSize = dxt1 ? 8 : 16;
	const auto* blocks = reader.Take(((w + 3) / 4) * ((height + 3) / 4) * blockSize);
	SpriteFrame image;
	if (pixels) image.RGBA.resize(std::size_t(w) * height * 4);
	for (unsigned by = 0; pixels && by < height; by += 4)
		for (unsigned bx = 0; bx < w; bx += 4, blocks += blockSize)
		{
			const auto* color = blocks + (dxt1 ? 0 : 8);
			const unsigned c0 = U16(color), c1 = U16(color + 2);
			Color colors[4]{RGB565(c0), RGB565(c1), {}, {}};
			for (int c = 0; c < 3; ++c)
			{
				colors[2][c] = dxt1 && c0 <= c1 ? (colors[0][c] + colors[1][c]) / 2 : (2 * colors[0][c] + colors[1][c]) / 3;
				colors[3][c] = (colors[0][c] + 2 * colors[1][c]) / 3;
			}
			colors[2][3] = 255;
			colors[3][3] = 255;
			if (dxt1 && c0 <= c1) colors[3] = {0, 0, 0, 0};
			unsigned char alpha[8]{blocks[0], blocks[1]};
			std::uint64_t indices = 0;
			if (dxt5)
			{
				const int end = alpha[0] > alpha[1] ? 8 : 6;
				for (int i = 2; i < end; ++i) alpha[i] = ((end - i) * alpha[0] + (i - 1) * alpha[1]) / (end - 1);
				if (end == 6) { alpha[6] = 0; alpha[7] = 255; }
				for (int i = 0; i < 6; ++i) indices |= std::uint64_t(blocks[i + 2]) << (8 * i);
			}
			const unsigned ci = U32(color + 4);
			for (unsigned p = 0; p < 16; ++p)
			{
				if (bx + p % 4 >= w || by + p / 4 >= height) continue;
				auto pixel = colors[(ci >> (p * 2)) & 3];
				if (dxt3) pixel[3] = ((blocks[p / 2] >> ((p % 2) * 4)) & 15) * 17;
				if (dxt5) pixel[3] = alpha[(indices >> (p * 3)) & 7];
				std::memcpy(image.RGBA.data() + (by + p / 4) * w * 4 + (bx + p % 4) * 4, pixel.data(), 4);
			}
		}
	// Complete DDS images include their lower mip levels. They are skipped, not mistaken for the next frame.
	unsigned mw = w, mh = height;
	const unsigned mips = std::max(1u, U32(h + 28));
	if (mips > 13) Invalid("DDS mip 层数超出范围");
	for (unsigned level = 1; level < mips; ++level)
	{
		if (mw == 1 && mh == 1) Invalid("DDS mip 层数与尺寸不符");
		mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
		reader.Take(((mw + 3) / 4) * ((mh + 3) / 4) * blockSize);
	}
	return image;
}
}
Sprite DecodeSpriteV3(std::span<const std::byte> bytes, bool pixels)
{
    Reader reader{bytes};
    const auto* h = reader.Take(40);
    if (std::memcmp(h, "IDSP", 4) || U32(h + 4) != 3) Invalid("Invalid IDSP v3 header");
    Sprite sprite;
    sprite.Type = U32(h + 8); sprite.Format = U32(h + 12);
    sprite.Width = U32(h + 20); sprite.Height = U32(h + 24);
    const unsigned count = U32(h + 28);
    std::memcpy(&sprite.Radius, h + 16, 4);
    std::memcpy(&sprite.BeamLength, h + 32, 4);
    sprite.SyncType = U32(h + 36);
    Dimensions(sprite.Width, sprite.Height);
    if (sprite.Type > 4 || sprite.Format > 3 || sprite.SyncType > 1 ||
        !std::isfinite(sprite.Radius) || sprite.Radius < 0 || !std::isfinite(sprite.BeamLength))
        Invalid("Invalid sprite metadata");
    if (!count || count > 2048 || std::uint64_t(sprite.Width) * sprite.Height * count * 4 > 256 * 1024 * 1024)
        Invalid("Sprite exceeds decoded frame budget");
    sprite.Frames.reserve(count);
    for (unsigned i = 0; i < count; ++i) sprite.Frames.push_back(DDS(reader, sprite.Width, sprite.Height, pixels));
    if (reader.Offset != bytes.size()) Invalid("Trailing SPR v3 data");
    return sprite;
}
}
