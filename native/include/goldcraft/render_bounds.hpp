#pragma once
#include <cmath>
#include <cstdint>

namespace goldcraft {
// Bounds of the exact world-space float positions submitted to the GPU.
// Build with a received mesh, then retain with that mesh's accepted revision.
class RenderBounds {
public:
    float min[3]{},max[3]{};

    void add(float x,float y,float z) noexcept {
        if(state_==State::invalid)return;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)){
            state_=State::invalid;return;
        }
        const float point[3]{x,y,z};
        if(state_==State::empty){
            for(unsigned i=0;i<3;++i)min[i]=max[i]=point[i];
            state_=State::bounded;return;
        }
        for(unsigned i=0;i<3;++i){
            if(point[i]<min[i])min[i]=point[i];
            if(point[i]>max[i])max[i]=point[i];
        }
    }

    // Empty and any batch containing a nonfinite transformed vertex both
    // return false. Callers must keep drawing when culling bounds are invalid.
    bool valid() const noexcept {return state_==State::bounded;}

private:
    enum class State : std::uint8_t {empty,bounded,invalid};
    State state_=State::empty;
};
}
