// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <vector>

// A transient, complete decoded file for the dedicated resource-advertisement
// pass. It never masquerades as a published Studio cache or survives the pass.
class GoldCraftStudioResourceFile
{
public:
    GoldCraftStudioResourceFile()=default;
    ~GoldCraftStudioResourceFile();
    GoldCraftStudioResourceFile(const GoldCraftStudioResourceFile&)=delete;
    GoldCraftStudioResourceFile& operator=(const GoldCraftStudioResourceFile&)=delete;
    bool Load(model_t* model);
    studiohdr_t* Header() const{return header_;}
private:
    unsigned char* file_{};
    std::vector<std::byte> expanded_;
    studiohdr_t* header_{};
};
