// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace csol
{
// Local x86 engine/game DLL interface. Pointers refer to engine-owned objects
// in this process only; no containers, borrowed spans or cache tickets cross it.
inline constexpr const char* SequenceServiceVersion="CSOLSequenceSafety001";
class ISequenceService : public IBaseInterface
{
public:
    virtual bool Tagged(void* model,const void* header) const=0;
    virtual void Select(void* model,const void* header,const void* descriptor) const=0;
    virtual void Sample(void* model,const void* header,const void* descriptor,
        const void* animation,float integerFrame,float fraction,bool wholeBlend) const=0;
};
}
