// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_death_services.h"
#include "scenario_runtime.h"
#include "scenario_player_runtime.h"
#include "scenario_drops.h"
#include "scenario_monster_damage.h"
#include "game_rules_gate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <atomic>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <locale>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

bool CCSOLScenarioDeathServices::Precache(csol::scenario::ResourceFileApi files,
    csol::scenario::ResourceError& error)
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->deathServices.get() != this || !runtime->session ||
        runtime->session->State() != CSOLScenarioSessionState::Precached) return false;
    const auto keepAlive = runtime->deathServices;
    const auto session = runtime->session;
    const auto access = session->Map().EntityAccess();
    const auto generation = runtime->Generation();
    const auto tables = session->Map().Tables();
    if (!tables) return false;
    m_cachedMapId = tables->mapId;
    const bool loaded = m_golden.Load(files, error);
    // File allocation/release belongs to the engine and can retire these rules.
    return !runtime->Closing() && runtime->Generation() == generation && runtime->session == session &&
        runtime->deathServices.get() == this && access->Allowed() &&
        session->Map().EntityAccess() == access && session->Map().Tables() == tables && loaded;
}

std::shared_ptr<CCSOLScenarioRuntime> CCSOLScenarioDeathServices::Pin(CSOLMonsterDeathCall& call) const
{
    const auto runtime = m_runtime.lock();
    if (!runtime || runtime->Closing() || runtime->deathServices.get() != this || !runtime->session ||
        !call.BelongsTo(runtime->session->Map()) || !call.Current()) return {};
    const auto tables = runtime->session->Map().Tables();
    if (!tables || tables->scenarioType != 1 || !m_cachedMapId || *m_cachedMapId <= 0 ||
        !tables->mapId || *tables->mapId <= 0 || !m_golden.EmptyPlan()) return {};
    return runtime;
}

std::shared_ptr<CCSOLScenarioPlayers> CCSOLScenarioDeathServices::Players() const
{
    const auto runtime = m_runtime.lock();
    return runtime && !runtime->Closing() && runtime->deathServices.get() == this && runtime->session ?
        runtime->session->Players() : nullptr;
}

bool CCSOLScenarioDeathServices::MoneyExtractor(CSOLMonsterDeathCall& call, const EHANDLE& handle,
    bool& enabled, csol::scenario::ConfigError& error)
{
    enabled = false;
    const auto runtime = Pin(call);
    if (!runtime) return false;
    const auto players = runtime->players;
    const auto player = players ? players->Find(handle) : nullptr;
    if (!player || handle.Get<CBaseEntity>() != call.Attacker())
    { error.message = "scenario kill money has no current player profile"; return false; }
    enabled = player->profile.moneyExtractor;
    return true;
}

bool CCSOLScenarioDeathServices::RoomOption(CSOLMonsterDeathCall& call, int& option,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return false;
    if (!runtime->room.moneyOption)
    { error.message = "scenario money extractor requires an authoritative room option"; return false; }
    option = *runtime->room.moneyOption;
    return true;
}

bool CCSOLScenarioDeathServices::RulesDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError&)
{
    // Verified Ver2 v32C = MP10611B90, ret8. Do not run the manager/root twice.
    return static_cast<bool>(Pin(call));
}

bool CCSOLScenarioDeathServices::Append(CSOLScenarioDeathRecord record, csol::scenario::ConfigError& error)
{
    if (m_records.size() >= 65536 || m_recordSequence == UINT64_MAX)
    { error.message = "scenario death statistics consumer has not saved its pending records"; return false; }
    record.sequence = ++m_recordSequence;
    m_records.push_back(std::move(record));
    return true;
}

bool CCSOLScenarioDeathServices::Acknowledge(std::uint64_t throughSequence)
{
    if (!throughSequence) return false;
    const auto found = std::find_if(m_records.begin(), m_records.end(),
        [throughSequence](const auto& record) { return record.sequence == throughSequence; });
    if (found == m_records.end()) return false;
    m_records.erase(m_records.begin(), found + 1);
    return true;
}

bool CCSOLScenarioDeathServices::PlayerKilledMonster(CSOLMonsterDeathCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = Pin(call);
    if (!runtime) return false;
    const auto keepAlive = runtime->deathServices;
    const auto session = runtime->session;
    const auto players = runtime->players;
    const auto drops = runtime->drops;
    const auto runtimeGeneration = runtime->Generation();
    auto* player = dynamic_cast<CBasePlayer*>(call.Attacker());
    if (!player || !players || !drops)
    { error.message = "scenario player death services are unavailable"; return false; }
    const EHANDLE handle(player);
    const auto state = players->Find(handle);
    if (!state) return false;
    const auto playerGeneration = state->generation;
    const auto ownerCurrent = [&]()
    {
        return Pin(call) == runtime && runtime->Generation() == runtimeGeneration &&
            runtime->session == session && runtime->players == players && runtime->drops == drops &&
            players->Active() && drops->Active();
    };
    const auto current = [&]()
    { return ownerCurrent() && players->Find(handle) == state && state->generation == playerGeneration &&
        call.Attacker() == player; };
    const auto fail = [&](const char* reason)
    { if (current() && error.message.empty()) error.message = reason; return false; };
    if (!current()) return false;
    if (m_deathSequence == UINT64_MAX) return fail("scenario death journal identity exhausted");
    CSOLScenarioDeathRecord base;
    base.death = ++m_deathSequence;
    base.mapGeneration = session->Map().SpawnPoints().Generation();
    base.roundGeneration = session->Map().RoundGeneration();
    base.playerGeneration = playerGeneration;

    const auto run = [&]() -> bool
    {
        if (!players->RootMonsterKilled(call, error) || !current()) return false;
        if (!CSOL_CompleteScenarioMonsterClearPoint(call, error) || !current()) return false;
        // Completion may enter Advancing/Completing. It does not invalidate this
        // death unless it actually closes/replaces the map, round or player.
        const int hitgroup = call.Current()->HostState().lastHitgroup;
        bool fragIncremented{};
        if (!players->AddMonsterFrag(call, fragIncremented, error) || !current()) return false;
        CSOLScenarioKillSource attribution;
        if (!players->ResolveKillSource(call, CSOLScenarioKillSourcePhase::Attribution, attribution, error) ||
            !current()) return false;

        // 11391690: the two Classify calls are observable, ordered calls. The
        // mode17 third call is outside this mode15 provider.
        std::uint16_t flags = (hitgroup == 1 ? 1 : 0) | (attribution.weapon == 29 ? 2 : 0);
        if (auto* inflictor = call.Inflictor())
        {
            const auto sourceCurrent = [&]()
            { return current() && call.Inflictor() == inflictor && !(inflictor->pev->flags & FL_KILLME); };
            if (!sourceCurrent()) return false;
            const int first = inflictor->Classify();
            if (!sourceCurrent()) return false;
            if (first == 21) flags = 0x20;
            else
            {
                const int second = inflictor->Classify();
                if (!sourceCurrent()) return false;
                if (second == 24) flags = 0x10;
            }
        }
        auto record = base;
        record.kind = CSOLScenarioDeathRecord::Kind::Kill;
        record.flags = flags;
        record.source = attribution.weapon;
        record.item = attribution.item;
        record.wireItem = static_cast<std::uint16_t>(attribution.item);
        record.attackerTeam = player->m_iTeam;
        record.monsterTeam = call.Current()->pev->team;
        if (!Append(record, error) || !current()) return false;
        if (fragIncremented)
        {
            record = base;
            record.kind = CSOLScenarioDeathRecord::Kind::Frags;
            record.attackerTeam = player->m_iTeam;
            const double frags = std::trunc(static_cast<double>(player->pev->frags));
            if (!std::isfinite(frags) || frags < std::numeric_limits<std::int32_t>::min() ||
                frags > std::numeric_limits<std::int32_t>::max())
                return fail("scenario statistics frags cannot be represented");
            record.frags = static_cast<std::int32_t>(frags);
            if (!Append(record, error) || !current()) return false;
        }
        const int classification = call.Current()->Classify();
        if (!current()) return false;
        if (classification != 17) return fail("scenario death statistics requires ordinary monster type access");
        const auto* type = call.Current()->Type();
        if (!type) return fail("scenario statistics lost the current monster type");
        if (type->eventClass)
        {
            record = base;
            record.kind = CSOLScenarioDeathRecord::Kind::Event20;
            record.event20 = {static_cast<std::uint16_t>(type->skill >= 1000),
                static_cast<std::uint16_t>(type->skill < 1000 && type->eventClass > 149)};
            if (!Append(record, error) || !current()) return false;
        }

        // Copy the pair at its original phase, after statistics callbacks. No
        // rescan, spawn capacity substitution, or alive-only approximation.
        const auto counts = runtime->ctCounts;
        if (!counts.valid) return fail("scenario drop CT snapshot has not been published");
        if (!drops->MonsterKilled(call, counts.alive, counts.total, error) || !current()) return false;
        const auto tables = session->Map().Tables();
        csol::scenario::AnnounceCandidates candidates;
        if (!players->ClassifyMonsterKill(call, *m_cachedMapId, *tables->mapId, candidates, error) ||
            !current()) return false;
        if (!candidates.Values().empty())
        {
            if (!runtime->room.noRibbon)
            {
                record = base;
                record.kind = CSOLScenarioDeathRecord::Kind::Ribbons;
                record.ribbons = candidates.ids;
                record.ribbonCount = candidates.count;
                if (!Append(record, error) || !current()) return false;
            }
            const auto messages = session->Messages();
            if (!messages.SendAnnounceRibbons(player->edict(), candidates.Values()))
                return fail("scenario ribbon client notification failed");
            if (!current()) return false;
        }
        CSOLScenarioKillSource notification;
        if (!players->ResolveKillSource(call, CSOLScenarioKillSourcePhase::Notification, notification, error) ||
            !current()) return false;
        return players->NotifyMonsterKill(call, notification.weapon, error) && current();
    };
    const bool completed = run();
    if (completed) return true;
    if (error.message.empty() && ownerCurrent() &&
        (players->Find(handle) != state || state->generation != playerGeneration || call.Attacker() != player))
    {
        // The player portion alone lost its recipient. Returning failure here
        // poisons DeathOwner/Requests and skips the already-dead monster's
        // cleanup. Keep prior records; never transfer them to a new generation.
        call.RevokeAttacker();
        return true;
    }
    return false;
}

bool CCSOLScenarioDeathServices::GoldenDeath(CSOLMonsterDeathCall& call, csol::scenario::ConfigError&)
{
    // This owner accepts only an actually empty configuration/plan/membership.
    // No replacement can have been installed, including for null attackers.
    // Nonempty configurations are rejected in Precache, never silently erased.
    return static_cast<bool>(Pin(call)) && m_golden.EmptyPlan();
}

bool CCSOLScenarioDeathServices::GoldenHit(CSOLMonsterDamageCall& call,
    csol::scenario::ConfigError& error)
{
    const auto runtime = m_runtime.lock();
    const auto session = runtime ? runtime->session : nullptr;
    const auto current = [&]
    { return runtime && !runtime->Closing() && runtime->deathServices.get() == this &&
        session && runtime->session == session && call.Spawn().BelongsTo(session->Map()) && call.Current(); };
    if (!current()) return call.Cancel();
    const auto keepAlive = runtime->deathServices;
    if (!call.Player()) return true;
    if (!m_golden.EmptyPlan())
    { error.message = "scenario Golden hit requires the loaded membership owner"; return false; }
    if (!g_engfuncs.pfnIndexOfEdict)
    { error.message = "scenario Golden hit target-index query is unavailable"; return false; }
    const int index = ENTINDEX(call.Current()->edict());
    if (!current()) return call.Cancel();
    if (!gpGlobals || index <= 0 || index >= gpGlobals->maxEntities)
    { error.message = "scenario Golden hit target index is invalid"; return false; }
    // The supported loaded EmptyPlan cannot have members. Original1097A620
    // still samples ENTINDEX before the missing-key return; it never reads
    // the attacker's opaque identity or inserts into a nonexistent node.
    return true;
}

namespace
{
std::atomic<std::uint64_t> archiveSequence{};

void JsonString(std::ostream& out, std::string_view text)
{
    constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    out << '"';
}

std::string ShutdownSnapshot(const CCSOLScenarioRuntime& runtime)
{
    const auto deaths = runtime.deathServices;
    const auto drops = runtime.drops;
    const auto players = runtime.players;
    const auto statistics = players ? players->StatisticsSnapshot() : std::vector<CSOLScenarioPlayerStatistics>{};
    if ((!deaths || deaths->Records().empty()) &&
        (!drops || (drops->Rewards().empty() && drops->PendingRewards().empty())) && statistics.empty() &&
        (!players || (players->KillPositions().empty() && players->KillChainRecords().empty()))) return {};
    std::ostringstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.imbue(std::locale::classic());
    out << "{\"format\":\"goldcraft.scenario.shutdown\",\"version\":1,"
        "\"identity_scope\":\"archive-directory/player-generation\","
        "\"float_encoding\":\"ieee754-binary32-unsigned-bits\",\"map\":";
    const auto* tables = runtime.session ? runtime.session->Map().Tables() : nullptr;
    const auto& cached = runtime.ArchiveMap();
    JsonString(out, cached ? cached->name : tables ? tables->mapName : "");
    out << ",\"map_id\":";
    const auto mapId = cached ? cached->id : tables ? tables->mapId : std::optional<int>{};
    if (mapId) out << *mapId; else out << "null";
    out << "}\n";
    if (deaths) for (const auto& value : deaths->Records())
    {
        out << "{\"channel\":\"death\",\"sequence\":" << value.sequence
            << ",\"death\":" << value.death << ",\"map_generation\":" << value.mapGeneration
            << ",\"round_generation\":" << value.roundGeneration << ",\"player_generation\":" << value.playerGeneration
            << ",\"kind\":" << static_cast<int>(value.kind) << ",\"source\":" << value.source
            << ",\"item\":" << value.item << ",\"flags\":" << value.flags << ",\"wire_item\":" << value.wireItem
            << ",\"attacker_team\":" << value.attackerTeam << ",\"monster_team\":" << value.monsterTeam
            << ",\"frags\":" << value.frags << ",\"event20\":[" << value.event20[0] << ',' << value.event20[1]
            << "],\"ribbon_count\":" << value.ribbonCount << ",\"ribbons\":[";
        for (std::size_t i = 0; i < value.ribbons.size(); ++i) { if (i) out << ','; out << unsigned(value.ribbons[i]); }
        out << "]}\n";
    }
    const auto reward = [&](const CSOLScenarioDropReward& value, bool submitted)
    {
        out << "{\"channel\":\"reward\",\"sequence\":" << value.sequence
            << ",\"player_generation\":" << value.playerGeneration << ",\"reward_id\":" << value.rewardId
            << ",\"submitted\":" << (submitted ? "true" : "false") << ",\"status\":";
        JsonString(out, value.status == CSOLScenarioDropReward::Status::AwaitingHandler ? "awaiting-handler" :
            value.status == CSOLScenarioDropReward::Status::Delivered ? "delivered" : "rejected");
        out << "}\n";
    };
    if (drops)
    {
        for (const auto& value : drops->Rewards()) reward(value, true);
        for (const auto& [ticket, value] : drops->PendingRewards()) reward(value, false);
    }
    if (players)
    {
        for (const auto& value : players->KillPositions())
            out << "{\"channel\":\"kill-position\",\"sequence\":" << value.sequence
                << ",\"map_generation\":" << value.mapGeneration << ",\"round_generation\":" << value.roundGeneration
                << ",\"player_generation\":" << value.playerGeneration << ",\"local_identity\":" << value.localIdentity
                << ",\"team\":" << value.team << ",\"source\":" << value.source
                << ",\"player\":" << (value.player ? "true" : "false")
                << ",\"killed\":" << (value.killed ? "true" : "false") << ",\"origin_bits\":["
                << std::bit_cast<std::uint32_t>(value.origin[0]) << ',' << std::bit_cast<std::uint32_t>(value.origin[1])
                << ',' << std::bit_cast<std::uint32_t>(value.origin[2]) << "]}\n";
        for (const auto& value : players->KillChainRecords())
            out << "{\"channel\":\"kill-chain\",\"sequence\":" << value.sequence
                << ",\"map_generation\":" << value.mapGeneration << ",\"round_generation\":" << value.roundGeneration
                << ",\"player_generation\":" << value.playerGeneration << ",\"local_identity\":" << value.localIdentity
                << ",\"kind\":" << static_cast<int>(value.kind) << "}\n";
    }
    for (const auto& value : statistics)
    {
        out << "{\"channel\":\"player-statistics\",\"player_generation\":" << value.generation
            << ",\"class_id\":" << value.classId << ",\"cap_event_sequence\":" << value.capEventSequence
            << ",\"cap_event_pending\":" << (value.capEventPending ? "true" : "false")
            << ",\"head_hits\":" << value.headHits << ",\"damage_hits\":" << value.damageHits
            << ",\"kill_chain\":" << value.killChain << ",\"best_kill_chain\":" << value.bestKillChain
            << ",\"boss_damage_bits\":" << std::bit_cast<std::uint32_t>(value.bossDamage)
            << ",\"ordinary_damage_bits\":" << std::bit_cast<std::uint32_t>(value.ordinaryDamage)
            << ",\"last_kill_at_bits\":" << std::bit_cast<std::uint32_t>(value.lastKillAt)
            << ",\"classification\":[" << value.classification.a << ',' << value.classification.b << ','
            << value.classification.c << ',' << value.classification.d << ',' << (value.classification.mixed ? "true" : "false")
            << "],\"weapon_kills\":[";
        bool first = true;
        for (const auto& [item, count] : value.weaponKills)
        { if (!first) out << ','; first = false; out << '[' << item << ',' << count << ']'; }
        out << "],\"weapon_damage_bits\":[";
        first = true;
        for (const auto& [item, damage] : value.weaponDamage)
        { if (!first) out << ','; first = false; out << '[' << item << ',' << std::bit_cast<std::uint32_t>(damage) << ']'; }
        out << "]}\n";
    }
    return out.str();
}

// Pin every Windows ancestor without FILE_SHARE_DELETE. On POSIX use only
// directory-relative operations on no-follow descriptors after traversal.
class ArchiveDirectory
{
public:
    ArchiveDirectory() = default;
    ArchiveDirectory(const ArchiveDirectory&) = delete;
    ArchiveDirectory& operator=(const ArchiveDirectory&) = delete;
    ~ArchiveDirectory()
    {
#if defined(_WIN32)
        for (auto handle : m_handles) CloseHandle(handle);
#else
        if (m_fd >= 0) ::close(m_fd);
#endif
    }

    bool Open(const std::filesystem::path& path)
    {
        if (!path.is_absolute()) return false;
        m_path = path.root_path();
#if defined(_WIN32)
        if (!Pin(m_path)) return false;
#else
        m_fd = ::open(m_path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (m_fd < 0) return false;
#endif
        for (const auto& part : path.relative_path())
            if (!part.empty() && !Append(part, false)) return false;
        return true;
    }

    bool Append(const std::filesystem::path& name, bool create, bool exclusive = false)
    {
        if (name.empty() || name != name.filename() || name == "." || name == "..") return false;
        auto next = m_path / name;
#if defined(_WIN32)
        if (create && !CreateDirectoryW(next.c_str(), nullptr) &&
            (exclusive || GetLastError() != ERROR_ALREADY_EXISTS)) return false;
        if (!Pin(next)) return false;
#else
        if (create && ::mkdirat(m_fd, name.c_str(), 0700) != 0 && (exclusive || errno != EEXIST)) return false;
        // Retry a previous mkdir/fsync failure too; existence is not durability.
        if (create && ::fsync(m_fd) != 0) return false;
        const int nextFd = ::openat(m_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (nextFd < 0) return false;
        ::close(m_fd);
        m_fd = nextFd;
#endif
        m_path.swap(next);
        return true;
    }

    const std::filesystem::path& Path() const { return m_path; }
#if !defined(_WIN32)
    int Fd() const { return m_fd; }
#endif
private:
    std::filesystem::path m_path;
#if defined(_WIN32)
    bool Pin(const std::filesystem::path& path)
    {
        const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { CloseHandle(handle); return false; }
        try { m_handles.push_back(handle); }
        catch (...) { CloseHandle(handle); throw; }
        return true;
    }
    std::vector<HANDLE> m_handles;
#else
    int m_fd{-1};
#endif
};

bool SameArchive(const ArchiveDirectory& directory, std::string_view bytes)
{
    std::array<char, 8192> buffer{};
    std::size_t offset{};
#if defined(_WIN32)
    const auto path = directory.Path() / "shutdown.jsonl";
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    bool matches = GetFileInformationByHandle(file, &info) && GetFileType(file) == FILE_TYPE_DISK &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        info.nNumberOfLinks == 1 && ((std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) == bytes.size();
    while (matches && offset < bytes.size())
    {
        const DWORD count = static_cast<DWORD>(std::min(buffer.size(), bytes.size() - offset));
        DWORD read{};
        matches = ReadFile(file, buffer.data(), count, &read, nullptr) && read == count &&
            std::string_view(buffer.data(), count) == bytes.substr(offset, count);
        offset += read;
    }
    const bool flushed = matches && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    return flushed && closed;
#else
    const int file = ::openat(directory.Fd(), "shutdown.jsonl", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (file < 0) return false;
    struct stat info{};
    bool matches = ::fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1 &&
        info.st_size >= 0 && static_cast<std::uint64_t>(info.st_size) == bytes.size();
    while (matches && offset < bytes.size())
    {
        const auto count = std::min(buffer.size(), bytes.size() - offset);
        const auto read = ::read(file, buffer.data(), count);
        if (read < 0 && errno == EINTR) continue;
        matches = read > 0 && std::string_view(buffer.data(), static_cast<std::size_t>(read)) ==
            bytes.substr(offset, static_cast<std::size_t>(read));
        if (read > 0) offset += static_cast<std::size_t>(read);
    }
    const bool flushed = matches && ::fsync(file) == 0;
    const bool closed = ::close(file) == 0;
    // A prior publication may have succeeded while directory fsync failed.
    return flushed && closed && ::fsync(directory.Fd()) == 0;
#endif
}

bool PublishArchive(const ArchiveDirectory& directory, std::string_view bytes)
{
    if (SameArchive(directory, bytes)) return true;
    const auto temporaryName = "pending-" + std::to_string(++archiveSequence) + ".tmp";
    bool committed{};
#if defined(_WIN32)
    const auto destination = directory.Path() / "shutdown.jsonl";
    const auto temporary = directory.Path() / temporaryName;
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::size_t offset{};
    while (offset < bytes.size())
    {
        DWORD written{};
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
        if (!WriteFile(file, bytes.data() + offset, requested, &written, nullptr) || !written) break;
        offset += written;
    }
    const bool flushed = offset == bytes.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    if (flushed && closed)
        committed = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    DeleteFileW(temporary.c_str());
#else
    const int file = ::openat(directory.Fd(), temporaryName.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) return false;
    std::size_t offset{};
    while (offset < bytes.size())
    {
        const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        offset += static_cast<std::size_t>(written);
    }
    const bool flushed = offset == bytes.size() && ::fsync(file) == 0;
    const bool closed = ::close(file) == 0;
#if defined(__linux__) && defined(SYS_renameat2)
    // RENAME_NOREPLACE publishes without a second hard link or overwriting a
    // different archive. An unsupported kernel keeps the in-memory records.
    constexpr unsigned renameNoReplace = 1;
    if (flushed && closed && ::syscall(SYS_renameat2, directory.Fd(), temporaryName.c_str(),
        directory.Fd(), "shutdown.jsonl", renameNoReplace) == 0)
        committed = ::fsync(directory.Fd()) == 0;
#endif
    ::unlinkat(directory.Fd(), temporaryName.c_str(), 0);
#endif
    return committed;
}
}

bool CSOL_ArchiveScenarioShutdown(const CCSOLScenarioRuntime& runtime,
    const std::string& root, std::string& directory, std::string& error)
{
    error.clear();
    try
    {
        if (!runtime.ReadyForArchive())
        { error = "scenario archive requires drained retired entities"; return false; }
        const auto bytes = ShutdownSnapshot(runtime);
        if (bytes.empty()) return true;
        if (root.empty()) { error = "scenario archive has no cached server game directory"; return false; }
        const auto rootPath = std::filesystem::absolute(root).lexically_normal();
        const auto base = rootPath / "csol" / "scenario" / "outbox";
        ArchiveDirectory pinned;
        if (!pinned.Open(rootPath) || !pinned.Append("csol", true) ||
            !pinned.Append("scenario", true) || !pinned.Append("outbox", true))
        { error = "scenario archive root is unavailable or redirected"; return false; }
        if (directory.empty())
        {
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            for (int attempt = 0; attempt < 32 && directory.empty(); ++attempt)
            {
                const auto name = std::to_string(stamp) + "-" + std::to_string(++archiveSequence);
                if (pinned.Append(name, true, true)) directory = pinned.Path().string();
            }
            if (directory.empty()) { error = "scenario archive could not allocate an independent session directory"; return false; }
        }
        else
        {
            const auto destination = std::filesystem::path(directory);
            if (destination.parent_path() != base || !pinned.Append(destination.filename(), false))
            { error = "scenario archive directory no longer belongs to this outbox"; return false; }
        }
        if (!PublishArchive(pinned, bytes))
        { error = "scenario archive could not commit matching shutdown records"; return false; }
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
    catch (...) { error = "scenario archive failed"; return false; }
}
