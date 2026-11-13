// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace guest::cof::server
{
// Host ownership/lifecycle capability. The canonical player runtime calls this
// once at the original-equivalent pre-input stage, including holstered items.
// It does not add another player clock or infer input from selected weapons.
class OwnerFrameWeapon
{
public:
    virtual void AdvanceOwnerFrame() noexcept=0;
    virtual void Invalidate() noexcept=0;
protected:
    ~OwnerFrameWeapon()=default;
};
}
