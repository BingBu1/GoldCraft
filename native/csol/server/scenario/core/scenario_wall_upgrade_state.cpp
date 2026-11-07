// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_wall_upgrade.h"

CCSOLScenarioWallUpgrade::CCSOLScenarioWallUpgrade(bool enabled,
    std::shared_ptr<CCSOLWallUpgradeServices> services,
    std::shared_ptr<const CSOLScenarioEntityAccess> access)
    : m_services(std::move(services)), m_access(std::move(access)), m_enabled(enabled) {}

bool CCSOLScenarioWallUpgrade::Current() const
{
    if (!m_valid || !m_access || !m_access->Allowed() || !m_services) return false;
    const auto generation = m_generation;
    const bool current = m_services->Current();
    return current && generation == m_generation && m_valid && m_access->Allowed();
}

CCSOLScenarioWallUpgrade::Result CCSOLScenarioWallUpgrade::Use(const EHANDLE& player)
{
    using namespace csol::wall_upgrade;
    const auto keepAlive = shared_from_this();
    if (!Current()) return Result::Cancelled;
    if (m_busy || m_level >= MaximumLevel) return Result::Ignored;
    const auto services = m_services;
    const auto generation = m_generation;
    const auto current = [&] {
        if (generation != m_generation || !Current()) return false;
        const bool playerCurrent = services->PlayerCurrent(player);
        return playerCurrent && generation == m_generation && Current();
    };
    if (!current()) return Result::Cancelled;
    struct Operation
    {
        bool& busy;
        explicit Operation(bool& value) : busy(value) { busy = true; }
        ~Operation() { busy = false; }
    } operation(m_busy);
    const auto checked = [&](bool result) {
        if (!current()) return Result::Cancelled;
        if (result) return Result::Upgraded;
        Invalidate();
        services->Failed("scenario wall upgrade service failed");
        return Result::Failed;
    };
    int account{};
    auto result = checked(services->ReadAccount(player, account));
    if (result != Result::Upgraded) return result;
    if (account < Price) return Result::Ignored;
    std::int16_t ctWins{};
    std::uint8_t participation{};
    result = checked(services->ReadEligibility(player, ctWins, participation));
    if (result != Result::Upgraded) return result;
    if (Check(m_level, account, ctWins, participation) == Eligibility::Participation)
    {
        result = checked(services->PublishDenied(player, DeniedText));
        return result == Result::Upgraded ? Result::Denied : result;
    }
    // MP reads the account again immediately before its setter. Engine or
    // service callbacks are not permitted to carry a stale account snapshot.
    result = checked(services->ReadAccount(player, account));
    if (result != Result::Upgraded) return result;
    // An invalid account from a host callback cannot overflow subtraction.
    if (account < 0) return checked(false);
    result = checked(services->WriteAccount(player, account - Price));
    if (result != Result::Upgraded) return result;
    ++m_level;
    result = checked(services->LevelChanged(m_level));
    if (result != Result::Upgraded) return result;
    result = checked(services->PublishState(m_enabled, m_level));
    if (result != Result::Upgraded) return result;
    result = checked(services->RecordServiceEvent(player, ExternalEvent));
    if (result != Result::Upgraded) return result;
    return checked(services->PublishUpgrader(player));
}

bool CCSOLScenarioWallUpgrade::ShowIndicator(const EHANDLE& machine)
{
    const auto keepAlive = shared_from_this();
    if (!Current()) return false;
    if (m_level >= csol::wall_upgrade::MaximumLevel) return true;
    const auto services = m_services;
    const auto generation = m_generation;
    const bool sent = services->ShowIndicator(machine);
    if (generation != m_generation || !Current()) return false;
    if (!sent) { Invalidate(); services->Failed("scenario wall upgrade indicator failed"); }
    return sent;
}

bool CCSOLScenarioWallUpgrade::RemoveIndicator(const EHANDLE& machine)
{
    const auto keepAlive = shared_from_this();
    if (!Current()) return false;
    const auto services = m_services;
    const auto generation = m_generation;
    const bool sent = services->RemoveIndicator(machine);
    if (generation != m_generation || !Current()) return false;
    if (!sent) { Invalidate(); services->Failed("scenario wall upgrade removal failed"); }
    return sent;
}
