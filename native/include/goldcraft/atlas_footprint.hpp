#pragma once
#include "atlas.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace goldcraft {
// Fixed UV dependencies for the exact quad UVs submitted to the section shader.
// No vertex traversal or allocation is needed when testing an animation packet.
class AtlasFootprint {
public:
    static constexpr unsigned tiles=64,max_dimension=4096;
    bool empty()const noexcept {
        for(const auto row:rows_)if(row)return false;return true;
    }
    bool intersects(const AtlasFootprint& other)const noexcept {
        for(unsigned y=0;y<tiles;++y)if(rows_[y]&other.rows_[y])return true;return false;
    }
    void unknown()noexcept {rows_.fill(~std::uint64_t{});}
    template<class Vertex> bool assign_quads(std::span<const Vertex> vertices,unsigned width,unsigned height)noexcept {
        rows_.fill(0);
        if(!dimensions(width,height)||vertices.size()%4){unknown();return false;}
        for(std::size_t first=0;first<vertices.size();first+=4){
            double min_u=1,max_u=0,min_v=1,max_v=0;
            for(unsigned corner=0;corner<4;++corner){
                const auto& vertex=vertices[first+corner];
                if(!std::isfinite(vertex.u)||!std::isfinite(vertex.v)){unknown();return false;}
                // Atlas textures use CLAMP_TO_EDGE, not repeat. Clamp before
                // expanding so extreme but finite exported UVs cannot overflow.
                const double u=std::clamp(double(vertex.u),0.0,1.0),v=std::clamp(double(vertex.v),0.0,1.0);
                min_u=std::min(min_u,u);max_u=std::max(max_u,u);min_v=std::min(min_v,v);max_v=std::max(max_v,v);
            }
            // One texel conservatively covers interpolation/edge precision.
            // Keep separate rectangles for separate quads: a section-wide
            // min/max would include unrelated sprites between its materials.
            const auto x0=tile(std::max(0.0,min_u-1.0/width)),x1=tile(std::min(1.0,max_u+1.0/width));
            const auto y0=tile(std::max(0.0,min_v-1.0/height)),y1=tile(std::min(1.0,max_v+1.0/height));
            add_rows(y0,y1,mask(x0,x1));
        }
        return true;
    }
    void add_texel(unsigned x,unsigned y,unsigned width,unsigned height)noexcept {
        if(!dimensions(width,height)||x>=width||y>=height){unknown();return;}
        // A texel in a small atlas spans several UV tiles. Include its whole
        // normalized interval, including the upper boundary conservatively.
        add_rows(pixel_first(y,height),pixel_last(y,height),mask(pixel_first(x,width),pixel_last(x,width)));
    }
private:
    friend class AtlasPixelTiles;
    std::array<std::uint64_t,tiles> rows_{};
    static bool dimensions(unsigned width,unsigned height)noexcept {
        return width&&height&&width<=max_dimension&&height<=max_dimension;
    }
    static unsigned tile(double coordinate)noexcept {return std::min(tiles-1,static_cast<unsigned>(coordinate*tiles));}
    static unsigned pixel_first(unsigned pixel,unsigned size)noexcept {return pixel*tiles/size;}
    static unsigned pixel_last(unsigned pixel,unsigned size)noexcept {return std::min(tiles-1,(pixel+1)*tiles/size);}
    static std::uint64_t mask(unsigned first,unsigned last)noexcept {
        return (~std::uint64_t{}<<first)&(~std::uint64_t{}>>(tiles-1-last));
    }
    void add_rows(unsigned first,unsigned last,std::uint64_t columns)noexcept {
        for(unsigned y=first;y<=last;++y)rows_[y]|=columns;
    }
};
static_assert(sizeof(AtlasFootprint)==512);

// Rebuilt only with the full atlas. Per changed pixel this avoids divisions;
// row ranges are computed once per changed patch row, outside all draw paths.
class AtlasPixelTiles {
public:
    void clear()noexcept {width_=height_=0;}
    bool reset(unsigned width,unsigned height)noexcept {
        clear();if(!AtlasFootprint::dimensions(width,height))return false;
        width_=width;height_=height;
        for(unsigned x=0;x<width;++x)columns_[x]=AtlasFootprint::mask(
            AtlasFootprint::pixel_first(x,width),AtlasFootprint::pixel_last(x,width));
        return true;
    }
    bool changes(std::span<const std::uint8_t> alpha,unsigned width,unsigned height,
                 std::span<const AtlasPatch> patches,AtlasFootprint& changed)const noexcept {
        changed={};
        if(!width_||width!=width_||height!=height_||alpha.size()!=std::size_t(width_)*height_){changed.unknown();return false;}
        for(const auto& patch:patches){
            if(!patch.width||!patch.height||patch.width>width_||patch.height>height_||
               patch.x>width_-patch.width||patch.y>height_-patch.height||
               patch.rgba.size()!=std::uint64_t(patch.width)*patch.height*4){changed.unknown();return false;}
            for(unsigned y=0;y<patch.height;++y){
                std::uint64_t columns=0;
                const auto row=std::size_t(patch.y+y)*width_+patch.x,source=std::size_t(y)*patch.width*4+3;
                for(unsigned x=0;x<patch.width;++x)
                    if(alpha[row+x]!=patch.rgba[source+x*4])columns|=columns_[patch.x+x];
                if(columns)changed.add_rows(AtlasFootprint::pixel_first(patch.y+y,height_),
                    AtlasFootprint::pixel_last(patch.y+y,height_),columns);
            }
        }
        // Compare every patch against the unchanged accepted alpha snapshot.
        // Overlap that restores an earlier value can over-invalidate, never
        // hide a final change. GPU and CPU commits keep wire order afterward.
        return true;
    }
private:
    unsigned width_{},height_{};
    std::array<std::uint64_t,AtlasFootprint::max_dimension> columns_{};
};
}
