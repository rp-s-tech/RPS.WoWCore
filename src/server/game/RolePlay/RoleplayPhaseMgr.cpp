#include "RoleplayPhaseMgr.h"

#include "CharacterCache.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QueryResult.h"
#include "RBAC.h"
#include "RoleplayDatabase.h"
#include "RoleplayPlayerTransitionEffects.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <fmt/format.h>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

using namespace std::string_view_literals;

namespace
{
RoleplayPhaseRole ParseRole(std::string const& value)
{
    if (value == "viewer")
        return RoleplayPhaseRole::Viewer;
    if (value == "editor" || value == "builder") // builder is accepted for legacy rows only.
        return RoleplayPhaseRole::Editor;
    if (value == "manager")
        return RoleplayPhaseRole::Manager;
    return RoleplayPhaseRole::None;
}

std::string EscapeJsonString(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char ch : value)
    {
        switch (ch)
        {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20)
                    escaped += fmt::format("\\u{:04x}", ch);
                else
                    escaped.push_back(char(ch));
                break;
        }
    }
    return escaped;
}

RoleplayPhaseSpawnType ParseSpawnType(std::string const& value, bool& valid)
{
    valid = true;
    if (value == "creature")
        return RoleplayPhaseSpawnType::Creature;
    if (value == "gameobject")
        return RoleplayPhaseSpawnType::GameObject;

    valid = false;
    return RoleplayPhaseSpawnType::Creature;
}

static_assert(RoleplayPhaseMgr::SharesExclusiveContext(0, 0));
static_assert(RoleplayPhaseMgr::SharesExclusiveContext(42, 42));
static_assert(!RoleplayPhaseMgr::SharesExclusiveContext(0, 42));
static_assert(!RoleplayPhaseMgr::SharesExclusiveContext(42, 43));
}

struct RoleplayPhaseMgr::Snapshot
{
    struct Phase
    {
        uint64 Id = 0;
        std::string Name;
        std::string Description;
        uint32 OwnerAccountId = 0;
        Optional<uint32> MapId;
        bool Enabled = false;
        bool IsPublic = false;
        Optional<uint32> SpawnMap;
        Optional<float> SpawnX;
        Optional<float> SpawnY;
        Optional<float> SpawnZ;
        Optional<float> SpawnO;
        bool EnterSpawn = false;
        bool Archived = false;
        bool Valid = false;
    };

    struct SpawnKey
    {
        RoleplayPhaseSpawnType Type = RoleplayPhaseSpawnType::Creature;
        uint64 SpawnId = 0;

        bool operator==(SpawnKey const& right) const
        {
            return Type == right.Type && SpawnId == right.SpawnId;
        }
    };

    struct SpawnKeyHash
    {
        size_t operator()(SpawnKey const& key) const
        {
            return std::hash<uint64>{}(key.SpawnId) ^ (size_t(key.Type) << 1);
        }
    };

    struct AddonKey
    {
        uint64 PhaseId = 0;
        std::string Key;

        bool operator==(AddonKey const& right) const
        {
            return PhaseId == right.PhaseId && Key == right.Key;
        }
    };

    struct AddonKeyHash
    {
        size_t operator()(AddonKey const& key) const
        {
            return std::hash<uint64>{}(key.PhaseId) ^ (std::hash<std::string>{}(key.Key) << 1);
        }
    };

    std::unordered_map<uint64, Phase> Phases;
    std::unordered_map<uint64, std::unordered_map<uint64, RoleplayPhaseRole>> MembersByPhase;
    std::unordered_map<uint64, uint64> ActivePhaseByCharacter;
    std::unordered_map<uint64, uint64> InvalidActivePhaseByCharacter;
    struct SpawnBinding
    {
        uint64 PhaseId = 0;
        uint32 MapId = 0;
    };

    std::unordered_map<SpawnKey, SpawnBinding, SpawnKeyHash> PhaseBySpawn;
    std::unordered_map<AddonKey, uint64, AddonKeyHash> AddonRevisions;
    std::unordered_map<AddonKey, RoleplayPhaseAddonData, AddonKeyHash> AddonData;
};

struct RoleplayPhaseMgr::LoadSummary
{
    uint32 PhaseRows = 0;
    uint32 MemberRows = 0;
    uint32 ActiveRows = 0;
    uint32 SpawnRows = 0;
    uint32 AddonRows = 0;
    uint32 LoadedPhases = 0;
    uint32 LoadedMembers = 0;
    uint32 LoadedActive = 0;
    uint32 LoadedSpawns = 0;
    uint32 LoadedAddonRevisions = 0;
    uint32 InvalidPhase = 0;
    uint32 InvalidMap = 0;
    uint32 MissingPhase = 0;
    uint32 MissingMember = 0;
    uint32 InvalidMember = 0;
    uint32 DuplicateActive = 0;
    uint32 InvalidActive = 0;
    uint32 OrphanSpawn = 0;
    uint32 InvalidSpawn = 0;
    uint32 InvalidAddonData = 0;
    uint32 QueryFailures = 0;

    uint32 Quarantined() const
    {
        return InvalidPhase + InvalidMap + MissingPhase + MissingMember + InvalidMember + DuplicateActive + InvalidActive
            + OrphanSpawn + InvalidSpawn + InvalidAddonData;
    }
};

RoleplayPhaseMgr& RoleplayPhaseMgr::Instance()
{
    static RoleplayPhaseMgr instance;
    return instance;
}

std::shared_ptr<RoleplayPhaseMgr::Snapshot const> RoleplayPhaseMgr::GetSnapshot() const
{
    return std::atomic_load(&_snapshot);
}

bool RoleplayPhaseMgr::WriteActivePhase(uint64 characterGuid, uint32 accountId, uint64 phaseId) const
{
    RoleplayDatabasePreparedStatement* stmt = nullptr;
    if (phaseId)
    {
        if (!accountId)
            return false;

        stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_REP_CHARACTER_RP_PHASE);
        stmt->setUInt64(0, characterGuid);
        stmt->setUInt32(1, accountId);
        stmt->setUInt64(2, phaseId);
    }
    else
    {
        stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_DEL_CHARACTER_RP_PHASE);
        stmt->setUInt64(0, characterGuid);
    }

    RoleplayDatabase.DirectExecute(stmt);
    return VerifyActivePhase(characterGuid, phaseId);
}

bool RoleplayPhaseMgr::VerifyActivePhase(uint64 characterGuid, uint64 phaseId) const
{
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_CHARACTER_RP_PHASE_BY_CHARACTER);
    stmt->setUInt64(0, characterGuid);
    PreparedQueryResult result = RoleplayDatabase.Query(stmt);
    if (!result)
        return false;

    Field* fields = result->Fetch();
    return fields[0].GetUInt64() == (phaseId ? 1u : 0u) && fields[1].GetUInt64() == phaseId;
}

bool RoleplayPhaseMgr::BuildActivePhaseSnapshot(std::shared_ptr<Snapshot>& snapshot,
    std::shared_ptr<Snapshot const> const& current, uint64 characterGuid, uint64 phaseId)
{
    if (!current)
        return false;

    snapshot = std::make_shared<Snapshot>(*current);
    if (phaseId)
        snapshot->ActivePhaseByCharacter[characterGuid] = phaseId;
    else
        snapshot->ActivePhaseByCharacter.erase(characterGuid);

    return true;
}

void RoleplayPhaseMgr::WriteTransitionAudit(uint64 characterGuid, uint32 accountId, std::string_view action,
    uint64 phaseId, std::string_view detail) const
{
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_AUDIT);
    stmt->setUInt64(0, characterGuid);
    stmt->setUInt32(1, accountId);
    stmt->setString(2, action);
    if (phaseId)
        stmt->setUInt64(3, phaseId);
    else
        stmt->setNull(3);
    stmt->setString(4, detail);
    RoleplayDatabase.DirectExecute(stmt);
}

bool RoleplayPhaseMgr::IsUsablePhase(Snapshot const& snapshot, uint64 phaseId)
{
    auto phase = snapshot.Phases.find(phaseId);
    return phase != snapshot.Phases.end() && phase->second.Valid && phase->second.Enabled && !phase->second.Archived;
}

bool RoleplayPhaseMgr::MapScopeAllows(Snapshot const& snapshot, uint64 phaseId, uint32 mapId)
{
    auto phase = snapshot.Phases.find(phaseId);
    return phase != snapshot.Phases.end() && (!phase->second.MapId || *phase->second.MapId == mapId);
}

bool RoleplayPhaseMgr::HasPhaseSpawn(Snapshot const& snapshot, uint64 phaseId)
{
    auto phase = snapshot.Phases.find(phaseId);
    if (phase == snapshot.Phases.end())
        return false;

    Snapshot::Phase const& value = phase->second;
    return value.SpawnMap && value.SpawnX && value.SpawnY && value.SpawnZ && value.SpawnO;
}

bool RoleplayPhaseMgr::Load()
{
    return ReloadInternal(false);
}

bool RoleplayPhaseMgr::Reload()
{
    return ReloadInternal(true);
}

bool RoleplayPhaseMgr::ReloadInternal(bool validateSpawns)
{
    std::vector<ContextReconciliation> reconciliations;
    {
        std::lock_guard<std::mutex> lock(_mutationMutex);

        uint32 const started = getMSTime();
        std::shared_ptr<Snapshot const> const previous = GetSnapshot();
        std::shared_ptr<Snapshot> candidate;
        LoadSummary summary;
        if (!BuildSnapshot(candidate, summary, validateSpawns))
        {
            TC_LOG_ERROR("roleplay.phase",
                "Roleplay phase reload failed before publication ({} query failures); keeping the previous snapshot.",
                summary.QueryFailures);
            return false;
        }

        std::shared_ptr<Snapshot const> const published = candidate;
        std::atomic_store(&_snapshot, std::move(candidate));
        CollectContextReconciliations(previous, published, reconciliations);

        TC_LOG_INFO("roleplay.phase",
            "Loaded RP phase snapshot: {} phases, {} members, {} active contexts, {} spawn mappings, {} addon revisions in {} ms{}.",
            summary.LoadedPhases, summary.LoadedMembers, summary.LoadedActive, summary.LoadedSpawns,
            summary.LoadedAddonRevisions, GetMSTimeDiffToNow(started), validateSpawns ? "" : " (spawn validation deferred)");

        if (summary.Quarantined())
        {
            TC_LOG_WARN("roleplay.phase",
                "RP phase load quarantined {} rows: invalid phase={}, invalid map={}, missing phase={}, invalid member={}, "
                "missing member={}, duplicate active={}, invalid active={}, orphan spawn={}, invalid spawn={}, invalid addon data={}.",
                summary.Quarantined(), summary.InvalidPhase, summary.InvalidMap, summary.MissingPhase,
                summary.InvalidMember, summary.MissingMember, summary.DuplicateActive, summary.InvalidActive, summary.OrphanSpawn,
                summary.InvalidSpawn, summary.InvalidAddonData);
        }
    }

    ApplyContextReconciliations(reconciliations);
    return true;
}

bool RoleplayPhaseMgr::BuildSnapshot(std::shared_ptr<Snapshot>& snapshot, LoadSummary& summary, bool validateSpawns) const
{
    snapshot = std::make_shared<Snapshot>();

    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASES);
        PreparedQueryResult result = RoleplayDatabase.Query(stmt);
        if (!result)
        {
            ++summary.QueryFailures;
            return false;
        }

        do
        {
            Field* fields = result->Fetch();
            if (fields[0].IsNull())
                continue;

            ++summary.PhaseRows;
            Snapshot::Phase phase;
            phase.Id = fields[0].GetUInt64();
            phase.Name = fields[1].GetString();
            phase.Description = fields[2].GetString();
            phase.OwnerAccountId = fields[3].GetUInt32();
            if (!fields[4].IsNull())
                phase.MapId = fields[4].GetUInt32();

            std::string const visibilityMode = fields[5].GetString();
            phase.Enabled = fields[6].GetBool();
            phase.IsPublic = fields[7].GetBool();
            if (!fields[8].IsNull())
                phase.SpawnMap = fields[8].GetUInt32();
            if (!fields[9].IsNull())
                phase.SpawnX = fields[9].GetFloat();
            if (!fields[10].IsNull())
                phase.SpawnY = fields[10].GetFloat();
            if (!fields[11].IsNull())
                phase.SpawnZ = fields[11].GetFloat();
            if (!fields[12].IsNull())
                phase.SpawnO = fields[12].GetFloat();
            phase.EnterSpawn = fields[13].GetBool();
            phase.Archived = !fields[15].IsNull();
            phase.Valid = phase.Id != 0 && visibilityMode == "exclusive";

            if (!phase.Valid)
                ++summary.InvalidPhase;

            if (phase.MapId && !sMapStore.LookupEntry(*phase.MapId))
            {
                phase.Valid = false;
                ++summary.InvalidMap;
            }
            if (phase.SpawnMap && (!sMapStore.LookupEntry(*phase.SpawnMap)
                || (phase.MapId && *phase.MapId != *phase.SpawnMap)))
            {
                phase.Valid = false;
                ++summary.InvalidMap;
            }

            if (!phase.Id)
                continue;

            auto [it, inserted] = snapshot->Phases.emplace(phase.Id, std::move(phase));
            if (!inserted)
            {
                ++summary.InvalidPhase;
                it->second.Valid = false;
            }
        } while (result->NextRow());
    }

    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASE_MEMBERS);
        PreparedQueryResult result = RoleplayDatabase.Query(stmt);
        if (!result)
        {
            ++summary.QueryFailures;
            return false;
        }

        do
        {
            Field* fields = result->Fetch();
            if (fields[0].IsNull())
                continue;

            ++summary.MemberRows;
            uint64 const phaseId = fields[0].GetUInt64();
            uint64 const characterGuid = fields[1].GetUInt64();
            RoleplayPhaseRole const role = ParseRole(fields[2].GetString());
            auto phase = snapshot->Phases.find(phaseId);
            if (phase == snapshot->Phases.end() || !phase->second.Valid)
            {
                ++summary.MissingPhase;
                continue;
            }

            if (!characterGuid || role == RoleplayPhaseRole::None)
            {
                ++summary.InvalidMember;
                continue;
            }

            auto [it, inserted] = snapshot->MembersByPhase[phaseId].emplace(characterGuid, role);
            if (!inserted)
            {
                ++summary.InvalidMember;
                continue;
            }

            ++summary.LoadedMembers;
        } while (result->NextRow());
    }

    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_CHARACTER_RP_PHASES);
        PreparedQueryResult result = RoleplayDatabase.Query(stmt);
        if (!result)
        {
            ++summary.QueryFailures;
            return false;
        }

        do
        {
            Field* fields = result->Fetch();
            if (fields[0].IsNull())
                continue;

            ++summary.ActiveRows;
            uint64 const characterGuid = fields[0].GetUInt64();
            uint32 accountId = fields[1].GetUInt32();
            uint64 const phaseId = fields[2].GetUInt64();
            if (!characterGuid)
            {
                ++summary.InvalidActive;
                continue;
            }

            if (!accountId)
                accountId = sCharacterCache->GetCharacterAccountIdByGuid(
                    ObjectGuid::Create<HighGuid::Player>(characterGuid));

            if (!IsUsablePhase(*snapshot, phaseId))
            {
                ++summary.MissingPhase;
                snapshot->InvalidActivePhaseByCharacter[characterGuid] = phaseId;
                continue;
            }

            Snapshot::Phase const& phase = snapshot->Phases.at(phaseId);
            auto memberIt = snapshot->MembersByPhase.find(phaseId);
            bool const hasMembership = memberIt != snapshot->MembersByPhase.end()
                && memberIt->second.contains(characterGuid);
            bool const isOwnerAccount = accountId && accountId == phase.OwnerAccountId;
            if (!phase.IsPublic && !hasMembership && !isOwnerAccount)
            {
                ++summary.MissingMember;
                if (accountId)
                    snapshot->InvalidActivePhaseByCharacter[characterGuid] = phaseId;
                continue;
            }

            auto [it, inserted] = snapshot->ActivePhaseByCharacter.emplace(characterGuid, phaseId);
            if (!inserted)
            {
                ++summary.DuplicateActive;
                continue;
            }

            ++summary.LoadedActive;
        } while (result->NextRow());
    }

    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASE_SPAWNS);
        PreparedQueryResult result = RoleplayDatabase.Query(stmt);
        if (!result)
        {
            ++summary.QueryFailures;
            return false;
        }

        do
        {
            Field* fields = result->Fetch();
            if (fields[0].IsNull())
                continue;

            ++summary.SpawnRows;
            uint64 const phaseId = fields[0].GetUInt64();
            bool validType = false;
            RoleplayPhaseSpawnType const type = ParseSpawnType(fields[1].GetString(), validType);
            uint64 const spawnId = fields[2].GetUInt64();
            uint32 const mapId = fields[3].GetUInt32();
            if (!validType || !spawnId || !IsUsablePhase(*snapshot, phaseId) || !sMapStore.LookupEntry(mapId))
            {
                ++summary.InvalidSpawn;
                continue;
            }

            if (!MapScopeAllows(*snapshot, phaseId, mapId))
            {
                ++summary.InvalidMap;
                continue;
            }

            // ObjectMgr has not loaded permanent spawns during Roleplay::LoadAllTables.
            // Do not publish unvalidated mappings: common is the fail-closed early state.
            if (!validateSpawns)
                continue;

            bool const existsOnExpectedMap = type == RoleplayPhaseSpawnType::Creature
                ? sObjectMgr->GetCreatureData(static_cast<ObjectGuid::LowType>(spawnId))
                    && sObjectMgr->GetCreatureData(static_cast<ObjectGuid::LowType>(spawnId))->mapId == mapId
                : sObjectMgr->GetGameObjectData(static_cast<ObjectGuid::LowType>(spawnId))
                    && sObjectMgr->GetGameObjectData(static_cast<ObjectGuid::LowType>(spawnId))->mapId == mapId;
            if (!existsOnExpectedMap)
            {
                ++summary.OrphanSpawn;
                continue;
            }

            Snapshot::SpawnKey const key{ type, spawnId };
            auto [it, inserted] = snapshot->PhaseBySpawn.emplace(key, Snapshot::SpawnBinding{ phaseId, mapId });
            if (!inserted)
            {
                ++summary.InvalidSpawn;
                continue;
            }

            ++summary.LoadedSpawns;
        } while (result->NextRow());
    }

    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASE_ADDON_DATA);
        PreparedQueryResult result = RoleplayDatabase.Query(stmt);
        if (!result)
        {
            ++summary.QueryFailures;
            return false;
        }

        do
        {
            Field* fields = result->Fetch();
            if (fields[0].IsNull())
                continue;

            ++summary.AddonRows;
            uint64 const phaseId = fields[0].GetUInt64();
            std::string const key = fields[1].GetString();
            std::string const value = fields[2].GetString();
            uint64 const revision = fields[3].GetUInt64();
            uint64 const updatedBy = fields[4].GetUInt64();
            if (!IsUsablePhase(*snapshot, phaseId) || key.empty() || !revision)
            {
                ++summary.InvalidAddonData;
                continue;
            }

            Snapshot::AddonKey addonKey{ phaseId, key };
            bool const revisionInserted = snapshot->AddonRevisions.emplace(addonKey, revision).second;
            bool const dataInserted = snapshot->AddonData.emplace(std::move(addonKey),
                RoleplayPhaseAddonData{ key, value, revision, updatedBy }).second;
            if (!revisionInserted || !dataInserted)
            {
                ++summary.InvalidAddonData;
                continue;
            }

            ++summary.LoadedAddonRevisions;
        } while (result->NextRow());
    }

    for (auto const& [phaseId, phase] : snapshot->Phases)
        if (phase.Valid && phase.Enabled && !phase.Archived)
            ++summary.LoadedPhases;

    return true;
}

uint64 RoleplayPhaseMgr::GetPlayerPhaseId(uint64 characterGuid) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot)
        return 0;

    auto active = snapshot->ActivePhaseByCharacter.find(characterGuid);
    return active != snapshot->ActivePhaseByCharacter.end() && IsUsablePhase(*snapshot, active->second) ? active->second : 0;
}

uint64 RoleplayPhaseMgr::GetPlayerPhaseId(uint64 characterGuid, uint32 mapId) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot)
        return 0;

    auto active = snapshot->ActivePhaseByCharacter.find(characterGuid);
    if (active == snapshot->ActivePhaseByCharacter.end() || !IsUsablePhase(*snapshot, active->second))
        return 0;

    return MapScopeAllows(*snapshot, active->second, mapId) ? active->second : 0;
}

uint64 RoleplayPhaseMgr::GetSpawnPhaseId(RoleplayPhaseSpawnType type, uint64 spawnId) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot)
        return 0;

    Snapshot::SpawnKey const key{ type, spawnId };
    auto binding = snapshot->PhaseBySpawn.find(key);
    return binding != snapshot->PhaseBySpawn.end() && IsUsablePhase(*snapshot, binding->second.PhaseId)
        ? binding->second.PhaseId : 0;
}

uint64 RoleplayPhaseMgr::GetSpawnPhaseId(RoleplayPhaseSpawnType type, uint64 spawnId, uint32 mapId) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !mapId)
        return 0;

    Snapshot::SpawnKey const key{ type, spawnId };
    auto binding = snapshot->PhaseBySpawn.find(key);
    if (binding == snapshot->PhaseBySpawn.end() || !IsUsablePhase(*snapshot, binding->second.PhaseId))
        return 0;

    return binding->second.MapId == mapId ? binding->second.PhaseId : 0;
}

bool RoleplayPhaseMgr::AssignPersistentSpawn(RoleplayPhaseSpawnType type, uint64 spawnId, uint32 mapId, uint64 phaseId)
{
    if (!spawnId)
        return false;

    uint32 const actualMapId = type == RoleplayPhaseSpawnType::Creature
        ? (sObjectMgr->GetCreatureData(static_cast<ObjectGuid::LowType>(spawnId))
                ? sObjectMgr->GetCreatureData(static_cast<ObjectGuid::LowType>(spawnId))->mapId : 0)
        : (sObjectMgr->GetGameObjectData(static_cast<ObjectGuid::LowType>(spawnId))
                ? sObjectMgr->GetGameObjectData(static_cast<ObjectGuid::LowType>(spawnId))->mapId : 0);
    return actualMapId == mapId && AssignPersistentSpawns(type, { spawnId }, phaseId);
}

bool RoleplayPhaseMgr::ClearPersistentSpawn(RoleplayPhaseSpawnType type, uint64 spawnId)
{
    return spawnId && ClearPersistentSpawns(type, { spawnId });
}

bool RoleplayPhaseMgr::AssignPersistentSpawns(RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds, uint64 phaseId)
{
    if (spawnIds.empty() || !phaseId)
        return false;

    std::vector<std::pair<uint64, uint32>> mappings;
    mappings.reserve(spawnIds.size());
    for (uint64 spawnId : spawnIds)
    {
        ObjectGuid::LowType const objectSpawnId = static_cast<ObjectGuid::LowType>(spawnId);
        uint32 const mapId = type == RoleplayPhaseSpawnType::Creature
            ? (sObjectMgr->GetCreatureData(objectSpawnId) ? sObjectMgr->GetCreatureData(objectSpawnId)->mapId : 0)
            : (sObjectMgr->GetGameObjectData(objectSpawnId) ? sObjectMgr->GetGameObjectData(objectSpawnId)->mapId : 0);
        if (!spawnId || !mapId || !sMapStore.LookupEntry(mapId))
            return false;
        mappings.emplace_back(spawnId, mapId);
    }

    std::sort(mappings.begin(), mappings.end());
    mappings.erase(std::unique(mappings.begin(), mappings.end()), mappings.end());

    std::lock_guard<std::mutex> lock(_mutationMutex);
    std::shared_ptr<Snapshot const> const current = GetSnapshot();
    if (!current || !IsUsablePhase(*current, phaseId))
        return false;
    for (auto const& [spawnId, mapId] : mappings)
        if (!MapScopeAllows(*current, phaseId, mapId))
            return false;

    std::shared_ptr<Snapshot> candidate;
    try
    {
        candidate = std::make_shared<Snapshot>(*current);
        for (auto const& [spawnId, mapId] : mappings)
            candidate->PhaseBySpawn[{ type, spawnId }] = Snapshot::SpawnBinding{ phaseId, mapId };
    }
    catch (std::exception const& exception)
    {
        TC_LOG_ERROR("roleplay.phase", "Failed to prepare {} persistent spawn mappings for phase {}: {}.",
            mappings.size(), phaseId, exception.what());
        return false;
    }

    RoleplayDatabaseTransaction transaction = RoleplayDatabase.BeginTransaction();
    for (auto const& [spawnId, mapId] : mappings)
    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_SPAWN);
        stmt->setUInt64(0, phaseId);
        stmt->setString(1, type == RoleplayPhaseSpawnType::Creature ? "creature"sv : "gameobject"sv);
        stmt->setUInt64(2, spawnId);
        stmt->setUInt32(3, mapId);
        transaction->Append(stmt);
    }
    RoleplayDatabase.DirectCommitTransaction(transaction);
    for (auto const& [spawnId, mapId] : mappings)
    {
        if (!VerifyPersistentSpawnMapping(type, spawnId, phaseId, mapId))
        {
            TC_LOG_ERROR("roleplay.phase",
                "Refusing to publish RP spawn mappings for phase {}: DB commit verification failed for spawn {}.",
                phaseId, spawnId);
            return false;
        }
    }
    std::atomic_store(&_snapshot, std::move(candidate));
    return true;
}

bool RoleplayPhaseMgr::ClearPersistentSpawns(RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds)
{
    if (spawnIds.empty())
        return false;

    std::vector<uint64> uniqueSpawnIds = spawnIds;
    std::sort(uniqueSpawnIds.begin(), uniqueSpawnIds.end());
    uniqueSpawnIds.erase(std::remove(uniqueSpawnIds.begin(), uniqueSpawnIds.end(), 0), uniqueSpawnIds.end());
    uniqueSpawnIds.erase(std::unique(uniqueSpawnIds.begin(), uniqueSpawnIds.end()), uniqueSpawnIds.end());
    if (uniqueSpawnIds.empty())
        return false;

    std::lock_guard<std::mutex> lock(_mutationMutex);
    std::shared_ptr<Snapshot const> const current = GetSnapshot();
    if (!current)
        return false;

    std::shared_ptr<Snapshot> candidate;
    try
    {
        candidate = std::make_shared<Snapshot>(*current);
        for (uint64 spawnId : uniqueSpawnIds)
            candidate->PhaseBySpawn.erase({ type, spawnId });
    }
    catch (std::exception const& exception)
    {
        TC_LOG_ERROR("roleplay.phase", "Failed to prepare removal of {} persistent spawn mappings: {}.",
            uniqueSpawnIds.size(), exception.what());
        return false;
    }

    RoleplayDatabaseTransaction transaction = RoleplayDatabase.BeginTransaction();
    for (uint64 spawnId : uniqueSpawnIds)
    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_DEL_RP_PHASE_SPAWN);
        stmt->setString(0, type == RoleplayPhaseSpawnType::Creature ? "creature"sv : "gameobject"sv);
        stmt->setUInt64(1, spawnId);
        transaction->Append(stmt);
    }
    RoleplayDatabase.DirectCommitTransaction(transaction);
    for (uint64 spawnId : uniqueSpawnIds)
    {
        if (!VerifyPersistentSpawnCleared(type, spawnId))
        {
            TC_LOG_ERROR("roleplay.phase",
                "Refusing to publish RP spawn clears: DB commit verification failed for spawn {}.", spawnId);
            return false;
        }
    }
    std::atomic_store(&_snapshot, std::move(candidate));
    return true;
}

bool RoleplayPhaseMgr::IsEnabled(uint64 phaseId) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    return snapshot && IsUsablePhase(*snapshot, phaseId);
}

bool RoleplayPhaseMgr::HasRole(uint64 phaseId, uint64 characterGuid, uint32 accountId, RoleplayPhaseRole required) const
{
    if (required == RoleplayPhaseRole::None)
        return true;

    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !IsUsablePhase(*snapshot, phaseId))
        return false;

    return GetMemberRole(phaseId, characterGuid, accountId) >= required;
}

RoleplayPhaseRole RoleplayPhaseMgr::GetMemberRole(uint64 phaseId, uint64 characterGuid, uint32 accountId) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !phaseId)
        return RoleplayPhaseRole::None;

    auto phase = snapshot->Phases.find(phaseId);
    if (phase == snapshot->Phases.end())
        return RoleplayPhaseRole::None;

    if (accountId && accountId == phase->second.OwnerAccountId)
        return RoleplayPhaseRole::Owner;

    auto members = snapshot->MembersByPhase.find(phaseId);
    if (members == snapshot->MembersByPhase.end())
        return RoleplayPhaseRole::None;

    auto member = members->second.find(characterGuid);
    return member != members->second.end() ? member->second : RoleplayPhaseRole::None;
}

bool RoleplayPhaseMgr::CanDiscover(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !phaseId || !snapshot->Phases.count(phaseId))
        return false;

    if (staffAccess)
        return true;

    // Owner/member always discover their phases (including archived soft-deleted ones).
    if (GetMemberRole(phaseId, characterGuid, accountId) >= RoleplayPhaseRole::Viewer)
        return true;

    Snapshot::Phase const& phase = snapshot->Phases.at(phaseId);
    return CanDiscoverUsablePhase(phase.IsPublic, IsUsablePhase(*snapshot, phaseId), false);
}

bool RoleplayPhaseMgr::CanView(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess) const
{
    return CanDiscover(phaseId, characterGuid, accountId, staffAccess);
}

bool RoleplayPhaseMgr::CanEnter(uint64 phaseId, uint64 characterGuid, uint32 accountId, uint32 mapId,
    bool staffAccess) const
{
    if (!phaseId)
        return true;

    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !IsUsablePhase(*snapshot, phaseId) || !MapScopeAllows(*snapshot, phaseId, mapId))
        return false;

    if (staffAccess)
        return true;

    Snapshot::Phase const& phase = snapshot->Phases.at(phaseId);
    if (phase.IsPublic)
        return true;

    // Account owner may enter without a membership row (create/set-owner write manager ACL only).
    if (accountId && accountId == phase.OwnerAccountId)
        return true;

    auto members = snapshot->MembersByPhase.find(phaseId);
    if (members == snapshot->MembersByPhase.end())
        return false;

    auto member = members->second.find(characterGuid);
    return member != members->second.end() && member->second >= RoleplayPhaseRole::Viewer;
}

bool RoleplayPhaseMgr::CanEdit(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess,
    bool serverStaff) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !phaseId || !IsUsablePhase(*snapshot, phaseId))
        return false;

    Snapshot::Phase const& phase = snapshot->Phases.at(phaseId);
    if (phase.OwnerAccountId == 0)
        return serverStaff;

    return staffAccess || GetMemberRole(phaseId, characterGuid, accountId) >= RoleplayPhaseRole::Editor;
}

bool RoleplayPhaseMgr::CanManage(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess,
    bool serverStaff) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !phaseId || !IsUsablePhase(*snapshot, phaseId))
        return false;

    Snapshot::Phase const& phase = snapshot->Phases.at(phaseId);
    if (phase.OwnerAccountId == 0)
        return serverStaff;

    return staffAccess || GetMemberRole(phaseId, characterGuid, accountId) >= RoleplayPhaseRole::Manager;
}

bool RoleplayPhaseMgr::CanOwnPhase(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess,
    bool serverStaff) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !phaseId || !IsUsablePhase(*snapshot, phaseId))
        return false;

    Snapshot::Phase const& phase = snapshot->Phases.at(phaseId);
    if (phase.OwnerAccountId == 0)
        return serverStaff;

    return staffAccess || GetMemberRole(phaseId, characterGuid, accountId) >= RoleplayPhaseRole::Owner;
}

bool RoleplayPhaseMgr::CanArchive(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess,
    bool serverStaff) const
{
    return CanOwnPhase(phaseId, characterGuid, accountId, staffAccess, serverStaff);
}

bool RoleplayPhaseMgr::CanUnarchive(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess,
    bool serverStaff) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !phaseId)
        return false;

    auto phase = snapshot->Phases.find(phaseId);
    if (phase == snapshot->Phases.end() || !phase->second.Valid || !phase->second.Archived)
        return false;

    if (phase->second.OwnerAccountId == 0)
        return serverStaff;

    return staffAccess || GetMemberRole(phaseId, characterGuid, accountId) >= RoleplayPhaseRole::Owner;
}

bool RoleplayPhaseMgr::IsOwnedOrMember(uint64 phaseId, uint64 characterGuid, uint32 accountId) const
{
    return GetMemberRole(phaseId, characterGuid, accountId) >= RoleplayPhaseRole::Viewer;
}

bool RoleplayPhaseMgr::IsOwnerAccount(uint64 phaseId, uint32 accountId) const
{
    if (!phaseId || !accountId)
        return false;

    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot)
        return false;

    auto phase = snapshot->Phases.find(phaseId);
    return phase != snapshot->Phases.end() && phase->second.OwnerAccountId == accountId;
}

bool RoleplayPhaseMgr::CanMutateCommonWorld(uint32 securityLevel)
{
    return securityLevel >= SEC_GAMEMASTER;
}

uint64 RoleplayPhaseMgr::GetAddonDataRevision(uint64 phaseId, std::string_view key) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !IsUsablePhase(*snapshot, phaseId))
        return 0;

    Snapshot::AddonKey const addonKey{ phaseId, std::string(key) };
    auto revision = snapshot->AddonRevisions.find(addonKey);
    return revision != snapshot->AddonRevisions.end() ? revision->second : 0;
}

bool RoleplayPhaseMgr::GetAddonData(uint64 phaseId, std::string_view key, RoleplayPhaseAddonData& data) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !IsUsablePhase(*snapshot, phaseId))
        return false;

    Snapshot::AddonKey const addonKey{ phaseId, std::string(key) };
    auto value = snapshot->AddonData.find(addonKey);
    if (value == snapshot->AddonData.end())
        return false;

    data = value->second;
    return true;
}

void RoleplayPhaseMgr::GetAddonDataByNamespace(uint64 phaseId, std::string_view nameSpace,
    std::vector<RoleplayPhaseAddonData>& data) const
{
    data.clear();
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !IsUsablePhase(*snapshot, phaseId))
        return;

    std::string prefix;
    if (!nameSpace.empty())
    {
        prefix.assign(nameSpace);
        prefix.push_back(':');
    }

    for (auto const& [key, value] : snapshot->AddonData)
        if (key.PhaseId == phaseId && (prefix.empty() || value.Key.starts_with(prefix)))
            data.push_back(value);
}

void RoleplayPhaseMgr::RegisterAddonMessageHandler(RoleplayPhaseAddonMessageHandler handler)
{
    _addonMessageHandler = handler;
}

bool RoleplayPhaseMgr::HandleAddonMessage(Player* player, uint32 chatType, std::string_view prefix,
    std::string_view message, bool selfWhisper) const
{
    return _addonMessageHandler && _addonMessageHandler(player, chatType, prefix, message, selfWhisper);
}

void RoleplayPhaseMgr::RegisterTransitionHandler(RoleplayPhaseTransitionHandler handler)
{
    _transitionHandler = handler;
}

bool RoleplayPhaseMgr::GetPhaseInfo(uint64 phaseId, RoleplayPhaseInfo& info) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot)
        return false;

    auto phase = snapshot->Phases.find(phaseId);
    if (phase == snapshot->Phases.end())
        return false;

    info.Id = phase->second.Id;
    info.Name = phase->second.Name;
    info.Description = phase->second.Description;
    info.OwnerAccountId = phase->second.OwnerAccountId;
    info.MapId = phase->second.MapId;
    info.Enabled = phase->second.Enabled;
    info.IsPublic = phase->second.IsPublic;
    info.SpawnMap = phase->second.SpawnMap;
    info.SpawnX = phase->second.SpawnX;
    info.SpawnY = phase->second.SpawnY;
    info.SpawnZ = phase->second.SpawnZ;
    info.SpawnO = phase->second.SpawnO;
    info.EnterSpawn = phase->second.EnterSpawn;
    info.Archived = phase->second.Archived;
    info.Valid = phase->second.Valid;
    info.Members.clear();

    if (auto members = snapshot->MembersByPhase.find(phaseId); members != snapshot->MembersByPhase.end())
        for (auto const& [characterGuid, role] : members->second)
            info.Members.push_back({ characterGuid, role });

    return true;
}

void RoleplayPhaseMgr::GetPhaseList(std::vector<RoleplayPhaseInfo>& phases) const
{
    phases.clear();
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot)
        return;

    phases.reserve(snapshot->Phases.size());
    for (auto const& [phaseId, phase] : snapshot->Phases)
    {
        RoleplayPhaseInfo info;
        info.Id = phase.Id;
        info.Name = phase.Name;
        info.Description = phase.Description;
        info.OwnerAccountId = phase.OwnerAccountId;
        info.MapId = phase.MapId;
        info.Enabled = phase.Enabled;
        info.IsPublic = phase.IsPublic;
        info.SpawnMap = phase.SpawnMap;
        info.SpawnX = phase.SpawnX;
        info.SpawnY = phase.SpawnY;
        info.SpawnZ = phase.SpawnZ;
        info.SpawnO = phase.SpawnO;
        info.EnterSpawn = phase.EnterSpawn;
        info.Archived = phase.Archived;
        info.Valid = phase.Valid;

        if (auto members = snapshot->MembersByPhase.find(phaseId); members != snapshot->MembersByPhase.end())
            for (auto const& [characterGuid, role] : members->second)
                info.Members.push_back({ characterGuid, role });

        phases.push_back(std::move(info));
    }
}

bool RoleplayPhaseMgr::GetSpawnInfo(RoleplayPhaseSpawnType type, uint64 spawnId, RoleplayPhaseSpawnInfo& info) const
{
    std::shared_ptr<Snapshot const> snapshot = GetSnapshot();
    if (!snapshot || !spawnId)
        return false;

    Snapshot::SpawnKey const key{ type, spawnId };
    auto mapping = snapshot->PhaseBySpawn.find(key);
    if (mapping == snapshot->PhaseBySpawn.end() || !IsUsablePhase(*snapshot, mapping->second.PhaseId))
        return false;

    info.PhaseId = mapping->second.PhaseId;
    info.Type = type;
    info.SpawnId = spawnId;
    info.MapId = mapping->second.MapId;
    return info.MapId != 0;
}

bool RoleplayPhaseMgr::Create(std::string const& name, std::string const& description, uint32 ownerAccountId,
    uint64 ownerCharacterGuid, uint64& phaseId, Optional<uint32> mapId)
{
    if (name.empty() || name.size() > 96 || description.size() > 255 || !ownerAccountId
        || !ownerCharacterGuid || (mapId && !sMapStore.LookupEntry(*mapId)))
        return false;

    phaseId = 0;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE);
    stmt->setString(0, name);
    stmt->setString(1, description);
    stmt->setUInt32(2, ownerAccountId);
    if (mapId)
        stmt->setUInt32(3, *mapId);
    else
        stmt->setNull(3);
    stmt->setString(4, "exclusive"sv);
    stmt->setBool(5, true);
    RoleplayDatabase.DirectExecute(stmt);

    phaseId = QueryLatestPhaseIdByMeta(name, description, ownerAccountId, mapId);
    if (!phaseId)
    {
        TC_LOG_ERROR("roleplay.phase", "Create RP phase failed: inserted row for '{}' was not readable by meta query.", name);
        return false;
    }

    stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_MEMBER);
    stmt->setUInt64(0, phaseId);
    stmt->setUInt64(1, ownerCharacterGuid);
    stmt->setString(2, "manager"sv);
    RoleplayDatabase.DirectExecute(stmt);
    return Reload();
}

bool RoleplayPhaseMgr::Archive(uint64 phaseId, uint64 actorCharacterGuid, uint32 actorAccountId, bool staffBypass,
    bool serverStaff)
{
    if (!CanArchive(phaseId, actorCharacterGuid, actorAccountId, staffBypass, serverStaff))
        return false;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_ARCHIVE);
    stmt->setUInt64(0, phaseId);
    RoleplayDatabase.DirectExecute(stmt);
    return Reload();
}

bool RoleplayPhaseMgr::Unarchive(uint64 phaseId, uint64 actorCharacterGuid, uint32 actorAccountId, bool staffBypass,
    bool serverStaff)
{
    if (!CanUnarchive(phaseId, actorCharacterGuid, actorAccountId, staffBypass, serverStaff))
        return false;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_UNARCHIVE);
    stmt->setUInt64(0, phaseId);
    RoleplayDatabase.DirectExecute(stmt);
    return Reload();
}

bool RoleplayPhaseMgr::RemoveMember(uint64 phaseId, uint64 characterGuid, uint64 actorCharacterGuid, uint32 actorAccountId,
    bool staffAccess, bool serverStaff)
{
    if (!characterGuid || !CanManage(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff))
        return false;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_DEL_RP_PHASE_MEMBER);
    stmt->setUInt64(0, phaseId);
    stmt->setUInt64(1, characterGuid);
    RoleplayDatabase.DirectExecute(stmt);
    return Reload();
}

bool RoleplayPhaseMgr::AssignSpawn(uint64 phaseId, RoleplayPhaseSpawnType type, uint64 spawnId,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess, bool serverStaff)
{
    return AssignSpawns(phaseId, type, { spawnId }, actorCharacterGuid, actorAccountId, staffAccess, serverStaff);
}

bool RoleplayPhaseMgr::AssignSpawns(uint64 phaseId, RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess, bool serverStaff)
{
    return !spawnIds.empty()
        && CanEdit(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff)
        && AssignPersistentSpawns(type, spawnIds, phaseId);
}

bool RoleplayPhaseMgr::ClearSpawn(RoleplayPhaseSpawnType type, uint64 spawnId,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess, bool serverStaff)
{
    return ClearSpawns(type, { spawnId }, actorCharacterGuid, actorAccountId, staffAccess, serverStaff);
}

bool RoleplayPhaseMgr::ClearSpawns(RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess, bool serverStaff)
{
    if (spawnIds.empty())
        return false;

    Optional<uint64> phaseId;
    for (uint64 spawnId : spawnIds)
    {
        RoleplayPhaseSpawnInfo info;
        if (!spawnId || !GetSpawnInfo(type, spawnId, info))
            return false;
        if (!phaseId)
            phaseId = info.PhaseId;
        else if (*phaseId != info.PhaseId)
            return false;
    }

    return phaseId && CanEdit(*phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff)
        && ClearPersistentSpawns(type, spawnIds);
}

bool RoleplayPhaseMgr::TransitionCharacter(uint64 characterGuid, uint64 phaseId, uint32 accountId, uint32 mapId)
{
    if (!characterGuid)
        return false;

    std::lock_guard<std::mutex> lock(_mutationMutex);
    std::shared_ptr<Snapshot const> const current = GetSnapshot();
    if (!current || (phaseId && !CanEnter(phaseId, characterGuid, accountId, mapId)))
        return false;

    uint64 const previousPhaseId = GetPlayerPhaseId(characterGuid);
    if (!WriteActivePhase(characterGuid, accountId, phaseId))
    {
        TC_LOG_ERROR("roleplay.phase", "Failed to persist RP phase transition for character {} to phase {}.", characterGuid, phaseId);
        return false;
    }

    std::shared_ptr<Snapshot> candidate;
    try
    {
        if (!BuildActivePhaseSnapshot(candidate, current, characterGuid, phaseId))
            throw std::runtime_error("missing active RP phase snapshot");
    }
    catch (std::exception const& exception)
    {
        TC_LOG_ERROR("roleplay.phase", "Failed to prepare RP phase transition snapshot for character {}: {}.", characterGuid, exception.what());
        if (!WriteActivePhase(characterGuid, accountId, previousPhaseId))
            TC_LOG_FATAL("roleplay.phase", "Failed to roll back RP phase transition for character {} to phase {}.", characterGuid, previousPhaseId);
        return false;
    }

    std::atomic_store(&_snapshot, std::move(candidate));
    return true;
}

bool RoleplayPhaseMgr::TransitionPlayer(Player* player, uint64 targetPhaseId, Optional<uint32> validationMapId)
{
    if (!player || !player->GetSession())
        return false;

    uint64 const characterGuid = player->GetGUID().GetCounter();
    if (!characterGuid)
        return false;

    uint32 const accountId = player->GetSession()->GetAccountId();
    uint64 previousPhaseId = 0;
    bool staffAccess = false;
    {
        std::lock_guard<std::mutex> lock(_mutationMutex);
        std::shared_ptr<Snapshot const> const current = GetSnapshot();
        uint32 const mapId = validationMapId.value_or(player->GetMapId());
        staffAccess = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
        if (!current || (targetPhaseId && !CanEnter(targetPhaseId, characterGuid, accountId, mapId, staffAccess)))
            return false;

        previousPhaseId = GetPlayerPhaseId(characterGuid);
        if (!WriteActivePhase(characterGuid, accountId, targetPhaseId))
        {
            TC_LOG_ERROR("roleplay.phase", "Failed to persist RP phase transition for player {} to phase {}.", player->GetGUID().ToString(), targetPhaseId);
            return false;
        }

        std::shared_ptr<Snapshot> candidate;
        try
        {
            if (!BuildActivePhaseSnapshot(candidate, current, characterGuid, targetPhaseId))
                throw std::runtime_error("missing active RP phase snapshot");
        }
        catch (std::exception const& exception)
        {
            TC_LOG_ERROR("roleplay.phase", "Failed to prepare RP phase transition snapshot for player {}: {}.", player->GetGUID().ToString(), exception.what());
            if (!WriteActivePhase(characterGuid, accountId, previousPhaseId))
                TC_LOG_FATAL("roleplay.phase", "Failed to roll back RP phase transition for player {} to phase {}.", player->GetGUID().ToString(), previousPhaseId);
            return false;
        }

        std::atomic_store(&_snapshot, std::move(candidate));
    }

    // Protocol/visibility side effects run only after the snapshot mutex is released.
    RoleplayPlayerTransitionEffects::Apply(player, previousPhaseId, targetPhaseId, _transitionHandler);
    if (staffAccess && targetPhaseId && GetMemberRole(targetPhaseId, characterGuid, player->GetSession()->GetAccountId()) < RoleplayPhaseRole::Viewer)
        WriteAudit(characterGuid, player->GetSession()->GetAccountId(), "staff_enter", targetPhaseId,
            R"({"source":"transition","bypass":"all_phases"})");
    return true;
}

bool RoleplayPhaseMgr::GotoPhase(Player* player, uint64 phaseId)
{
    if (!player || !player->GetSession() || !phaseId)
        return false;

    RoleplayPhaseInfo phase;
    if (!GetPhaseInfo(phaseId, phase) || !phase.HasSpawn())
        return false;

    uint64 const characterGuid = player->GetGUID().GetCounter();
    uint32 const accountId = player->GetSession()->GetAccountId();
    bool const staffAccess = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
    if (!CanEnter(phaseId, characterGuid, accountId, *phase.SpawnMap, staffAccess))
        return false;

    uint64 const previousPhaseId = GetPlayerPhaseId(characterGuid);
    if (!TransitionPlayer(player, phaseId, *phase.SpawnMap))
        return false;

    if (player->TeleportTo(*phase.SpawnMap, *phase.SpawnX, *phase.SpawnY, *phase.SpawnZ, *phase.SpawnO))
    {
        WriteAudit(characterGuid, accountId, "goto_phase", phaseId,
            fmt::format(R"({{"map":{},"spawn":true}})", *phase.SpawnMap));
        return true;
    }

    bool const rolledBack = TransitionPlayer(player, previousPhaseId);
    WriteAudit(characterGuid, accountId, "goto_phase_teleport_failed", phaseId,
        fmt::format(R"({{"rollback":{},"previous_phase":{}}})", rolledBack ? "true" : "false", previousPhaseId));
    return false;
}

bool RoleplayPhaseMgr::RestorePlayerContext(Player* player)
{
    if (!player || !player->GetSession())
        return false;

    uint64 const characterGuid = player->GetGUID().GetCounter();
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_CHARACTER_RP_PHASE_BY_CHARACTER);
    stmt->setUInt64(0, characterGuid);
    PreparedQueryResult result = RoleplayDatabase.Query(stmt);
    if (!result)
    {
        TC_LOG_ERROR("roleplay.phase", "Unable to restore RP phase context for player {}: active context query failed.", player->GetGUID().ToString());
        return false;
    }

    Field* fields = result->Fetch();
    uint64 const persistedPhaseId = fields[1].GetUInt64();
    if (!fields[0].GetUInt64())
        return true;

    if (CanEnter(persistedPhaseId, characterGuid, player->GetSession()->GetAccountId(), player->GetMapId()))
        return TransitionPlayer(player, persistedPhaseId);

    if (!TransitionPlayer(player, 0))
        return false;

    WriteTransitionAudit(characterGuid, player->GetSession()->GetAccountId(), "login_restore_invalid_context"sv,
        persistedPhaseId, R"({"reason":"acl_archive_or_map_scope"})"sv);
    TC_LOG_WARN("roleplay.phase", "Cleared invalid persisted RP phase {} for player {} during login restore.",
        persistedPhaseId, player->GetGUID().ToString());
    return true;
}

bool RoleplayPhaseMgr::SetActive(uint64 characterGuid, uint64 phaseId, uint32 accountId, uint32 mapId)
{
    if (!characterGuid)
        return false;

    if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(characterGuid)))
        return TransitionPlayer(player, phaseId);

    return TransitionCharacter(characterGuid, phaseId, accountId, mapId);
}

bool RoleplayPhaseMgr::AddMember(uint64 phaseId, uint64 characterGuid, RoleplayPhaseRole role,
    uint64 actorCharacterGuid, uint32 actorAccountId)
{
    return SetMemberRole(phaseId, characterGuid, role, actorCharacterGuid, actorAccountId, false, false);
}

bool RoleplayPhaseMgr::VerifyPhaseMetadata(uint64 phaseId, RoleplayPhaseInfo const& expected) const
{
    RoleplayPhaseInfo actual;
    return GetPhaseInfo(phaseId, actual)
        && actual.OwnerAccountId == expected.OwnerAccountId
        && actual.IsPublic == expected.IsPublic
        && actual.SpawnMap == expected.SpawnMap
        && actual.SpawnX == expected.SpawnX
        && actual.SpawnY == expected.SpawnY
        && actual.SpawnZ == expected.SpawnZ
        && actual.SpawnO == expected.SpawnO
        && actual.EnterSpawn == expected.EnterSpawn;
}

bool RoleplayPhaseMgr::SetPublic(uint64 phaseId, bool isPublic, uint64 actorCharacterGuid, uint32 actorAccountId,
    bool staffAccess, bool serverStaff)
{
    if (!CanOwnPhase(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff))
        return false;

    RoleplayPhaseInfo expected;
    if (!GetPhaseInfo(phaseId, expected))
        return false;
    expected.IsPublic = isPublic;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_PUBLIC);
    stmt->setBool(0, isPublic);
    stmt->setUInt64(1, phaseId);
    RoleplayDatabase.DirectExecute(stmt);
    if (!Reload() || !VerifyPhaseMetadata(phaseId, expected))
        return false;

    bool const staffMutation = staffAccess || (serverStaff && expected.OwnerAccountId == 0);
    WriteAudit(actorCharacterGuid, actorAccountId, staffMutation ? "set_public_staff" : "set_public", phaseId,
        isPublic ? R"({"public":true})" : R"({"public":false})");
    return true;
}

bool RoleplayPhaseMgr::Rename(uint64 phaseId, std::string const& name, uint64 actorCharacterGuid, uint32 actorAccountId,
    bool staffAccess, bool serverStaff)
{
    std::string trimmed = name;
    while (!trimmed.empty() && trimmed.front() == ' ')
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && trimmed.back() == ' ')
        trimmed.pop_back();
    if (trimmed.empty() || trimmed.size() > 96
        || !CanOwnPhase(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff))
        return false;

    RoleplayPhaseInfo expected;
    if (!GetPhaseInfo(phaseId, expected))
        return false;
    expected.Name = trimmed;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_NAME);
    stmt->setString(0, trimmed);
    stmt->setUInt64(1, phaseId);
    RoleplayDatabase.DirectExecute(stmt);
    if (!Reload())
        return false;

    RoleplayPhaseInfo actual;
    if (!GetPhaseInfo(phaseId, actual) || actual.Name != trimmed)
        return false;

    bool const staffMutation = staffAccess || (serverStaff && expected.OwnerAccountId == 0);
    WriteAudit(actorCharacterGuid, actorAccountId, staffMutation ? "rename_staff" : "rename", phaseId,
        fmt::format(R"({{"name":"{}"}})", EscapeJsonString(trimmed)));
    return true;
}

bool RoleplayPhaseMgr::SetOwner(uint64 phaseId, uint32 newOwnerAccountId, uint64 managerCharacterGuid,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool force, bool staffAccess, bool serverStaff)
{
    // force is required by the chat `.rps phase set owner … force` path; addon/GM shorthand
    // may pass force=true after its own confirmation. ACL remains CanOwnPhase / ServerStaff.
    if (!force || !CanOwnPhase(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff)
        || (newOwnerAccountId && !managerCharacterGuid) || (!newOwnerAccountId && !serverStaff))
        return false;

    RoleplayPhaseInfo expected;
    if (!GetPhaseInfo(phaseId, expected))
        return false;
    bool const serverOwnedBefore = expected.OwnerAccountId == 0;
    expected.OwnerAccountId = newOwnerAccountId;

    RoleplayDatabaseTransaction transaction = RoleplayDatabase.BeginTransaction();
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_OWNER);
    stmt->setUInt32(0, newOwnerAccountId);
    stmt->setUInt64(1, phaseId);
    transaction->Append(stmt);

    if (managerCharacterGuid)
    {
        stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_MEMBER);
        stmt->setUInt64(0, phaseId);
        stmt->setUInt64(1, managerCharacterGuid);
        stmt->setString(2, "manager"sv);
        transaction->Append(stmt);
    }
    RoleplayDatabase.DirectCommitTransaction(transaction);

    if (!Reload() || !VerifyPhaseMetadata(phaseId, expected)
        || (managerCharacterGuid && GetMemberRole(phaseId, managerCharacterGuid, 0) < RoleplayPhaseRole::Manager))
        return false;

    bool const staffMutation = staffAccess || (serverStaff && (serverOwnedBefore || !newOwnerAccountId));
    WriteAudit(actorCharacterGuid, actorAccountId, staffMutation ? "set_owner_staff" : "set_owner", phaseId,
        fmt::format(R"({{"owner_account":{},"manager_character":{},"force":true}})", newOwnerAccountId, managerCharacterGuid));
    return true;
}

bool RoleplayPhaseMgr::SetMemberRole(uint64 phaseId, uint64 characterGuid, RoleplayPhaseRole role,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess, bool serverStaff)
{
    if (!characterGuid || role < RoleplayPhaseRole::Viewer || role > RoleplayPhaseRole::Manager
        || !CanManage(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff))
        return false;

    // Managers may grant only viewer/editor. Owner (and staff) may grant manager.
    if (role == RoleplayPhaseRole::Manager
        && !staffAccess
        && GetMemberRole(phaseId, actorCharacterGuid, actorAccountId) < RoleplayPhaseRole::Owner)
        return false;

    RoleplayPhaseInfo phase;
    if (!GetPhaseInfo(phaseId, phase))
        return false;
    char const* roleText = role == RoleplayPhaseRole::Viewer ? "viewer"
        : role == RoleplayPhaseRole::Editor ? "editor" : "manager";
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_INS_RP_PHASE_MEMBER);
    stmt->setUInt64(0, phaseId);
    stmt->setUInt64(1, characterGuid);
    stmt->setString(2, std::string_view(roleText));
    RoleplayDatabase.DirectExecute(stmt);
    if (!Reload() || GetMemberRole(phaseId, characterGuid, 0) != role)
        return false;

    bool const staffMutation = staffAccess || (serverStaff && phase.OwnerAccountId == 0);
    WriteAudit(actorCharacterGuid, actorAccountId, staffMutation ? "set_member_role_staff" : "set_member_role", phaseId,
        fmt::format(R"({{"character":{},"role":"{}"}})", characterGuid, roleText));
    return true;
}

bool RoleplayPhaseMgr::SetEnterSpawnPoint(uint64 phaseId, uint32 mapId, float x, float y, float z, float o,
    uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess, bool serverStaff)
{
    if (!mapId || !sMapStore.LookupEntry(mapId)
        || !CanManage(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff))
        return false;

    RoleplayPhaseInfo expected;
    if (!GetPhaseInfo(phaseId, expected) || (expected.MapId && *expected.MapId != mapId))
        return false;
    expected.SpawnMap = mapId;
    expected.SpawnX = x;
    expected.SpawnY = y;
    expected.SpawnZ = z;
    expected.SpawnO = o;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_ENTER_SPAWN);
    stmt->setUInt32(0, mapId);
    stmt->setFloat(1, x);
    stmt->setFloat(2, y);
    stmt->setFloat(3, z);
    stmt->setFloat(4, o);
    stmt->setBool(5, expected.EnterSpawn);
    stmt->setUInt64(6, phaseId);
    RoleplayDatabase.DirectExecute(stmt);
    if (!Reload() || !VerifyPhaseMetadata(phaseId, expected))
        return false;

    bool const staffMutation = staffAccess || (serverStaff && expected.OwnerAccountId == 0);
    WriteAudit(actorCharacterGuid, actorAccountId, staffMutation ? "set_enter_spawn_staff" : "set_enter_spawn", phaseId,
        fmt::format(R"({{"map":{},"x":{},"y":{},"z":{},"o":{}}})", mapId, x, y, z, o));
    return true;
}

bool RoleplayPhaseMgr::SetEnterSpawnEnabled(uint64 phaseId, bool enabled, uint64 actorCharacterGuid, uint32 actorAccountId,
    bool staffAccess, bool serverStaff)
{
    if (!CanManage(phaseId, actorCharacterGuid, actorAccountId, staffAccess, serverStaff))
        return false;

    RoleplayPhaseInfo expected;
    if (!GetPhaseInfo(phaseId, expected) || (enabled && !expected.HasSpawn()))
        return false;
    expected.EnterSpawn = enabled;

    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_UPD_RP_PHASE_ENTER_SPAWN);
    if (expected.SpawnMap)
        stmt->setUInt32(0, *expected.SpawnMap);
    else
        stmt->setNull(0);
    if (expected.SpawnX) stmt->setFloat(1, *expected.SpawnX); else stmt->setNull(1);
    if (expected.SpawnY) stmt->setFloat(2, *expected.SpawnY); else stmt->setNull(2);
    if (expected.SpawnZ) stmt->setFloat(3, *expected.SpawnZ); else stmt->setNull(3);
    if (expected.SpawnO) stmt->setFloat(4, *expected.SpawnO); else stmt->setNull(4);
    stmt->setBool(5, enabled);
    stmt->setUInt64(6, phaseId);
    RoleplayDatabase.DirectExecute(stmt);
    if (!Reload() || !VerifyPhaseMetadata(phaseId, expected))
        return false;

    bool const staffMutation = staffAccess || (serverStaff && expected.OwnerAccountId == 0);
    WriteAudit(actorCharacterGuid, actorAccountId, staffMutation ? "set_enter_spawn_enabled_staff" : "set_enter_spawn_enabled",
        phaseId, enabled ? R"({"enter_spawn":true})" : R"({"enter_spawn":false})");
    return true;
}

void RoleplayPhaseMgr::WriteAudit(uint64 actorCharacterGuid, uint32 actorAccountId, std::string_view action,
    uint64 phaseId, std::string_view detail) const
{
    WriteTransitionAudit(actorCharacterGuid, actorAccountId, action, phaseId, detail);
}

void RoleplayPhaseMgr::CollectContextReconciliations(std::shared_ptr<Snapshot const> const& previous,
    std::shared_ptr<Snapshot const> const& current, std::vector<ContextReconciliation>& out) const
{
    out.clear();
    if (!current)
        return;

    for (auto const& [characterGuid, invalidPhaseId] : current->InvalidActivePhaseByCharacter)
    {
        ContextReconciliation item;
        item.CharacterGuid = characterGuid;
        item.PreviousPhaseId = invalidPhaseId;
        item.ClearPersisted = true;
        item.AuditAction = "reconcile_invalid_context";
        out.push_back(std::move(item));
    }

    if (!previous)
        return;

    for (auto const& [characterGuid, previousPhaseId] : previous->ActivePhaseByCharacter)
    {
        if (!characterGuid || !IsUsablePhase(*previous, previousPhaseId))
            continue;
        if (current->InvalidActivePhaseByCharacter.contains(characterGuid))
            continue;

        uint64 currentPhaseId = 0;
        auto active = current->ActivePhaseByCharacter.find(characterGuid);
        if (active != current->ActivePhaseByCharacter.end() && IsUsablePhase(*current, active->second))
            currentPhaseId = active->second;

        if (currentPhaseId == previousPhaseId)
            continue;

        ContextReconciliation item;
        item.CharacterGuid = characterGuid;
        item.PreviousPhaseId = previousPhaseId;
        item.CurrentPhaseId = currentPhaseId;
        item.ClearPersisted = currentPhaseId == 0;
        item.AuditAction = currentPhaseId == 0 ? "reconcile_invalid_context" : "reconcile_context_changed";
        out.push_back(std::move(item));
    }
}

void RoleplayPhaseMgr::ApplyContextReconciliations(std::vector<ContextReconciliation> const& reconciliations)
{
    for (ContextReconciliation const& item : reconciliations)
    {
        if (item.ClearPersisted && !WriteActivePhase(item.CharacterGuid, 0, 0))
        {
            TC_LOG_ERROR("roleplay.phase",
                "Failed to clear persisted invalid RP context for character {} (previous phase {}).",
                item.CharacterGuid, item.PreviousPhaseId);
        }

        if (!item.AuditAction.empty())
            WriteTransitionAudit(item.CharacterGuid, 0, item.AuditAction, item.PreviousPhaseId,
                R"({"source":"snapshot_reconcile"})");

        if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(item.CharacterGuid)))
            RoleplayPlayerTransitionEffects::Apply(player, item.PreviousPhaseId, item.CurrentPhaseId, _transitionHandler);
    }
}

bool RoleplayPhaseMgr::VerifyPersistentSpawnMapping(RoleplayPhaseSpawnType type, uint64 spawnId, uint64 expectedPhaseId,
    uint32 expectedMapId) const
{
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASE_SPAWN_BY_KEY);
    stmt->setString(0, type == RoleplayPhaseSpawnType::Creature ? "creature"sv : "gameobject"sv);
    stmt->setUInt64(1, spawnId);
    PreparedQueryResult result = RoleplayDatabase.Query(stmt);
    if (!result)
        return false;

    Field* fields = result->Fetch();
    return fields[0].GetUInt64() == expectedPhaseId && fields[1].GetUInt32() == expectedMapId;
}

bool RoleplayPhaseMgr::VerifyPersistentSpawnCleared(RoleplayPhaseSpawnType type, uint64 spawnId) const
{
    RoleplayDatabasePreparedStatement* stmt =
        RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASE_SPAWN_COUNT_BY_KEY);
    stmt->setString(0, type == RoleplayPhaseSpawnType::Creature ? "creature"sv : "gameobject"sv);
    stmt->setUInt64(1, spawnId);
    PreparedQueryResult result = RoleplayDatabase.Query(stmt);
    return result && result->Fetch()[0].GetUInt64() == 0;
}

uint64 RoleplayPhaseMgr::QueryLatestPhaseIdByMeta(std::string const& name, std::string const& description,
    uint32 ownerAccountId, Optional<uint32> mapId) const
{
    RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_SEL_RP_PHASE_LATEST_BY_META);
    stmt->setString(0, name);
    stmt->setString(1, description);
    stmt->setUInt32(2, ownerAccountId);
    uint32 const mapKey = mapId.value_or(0);
    stmt->setUInt32(3, mapKey);
    stmt->setUInt32(4, mapKey);
    PreparedQueryResult result = RoleplayDatabase.Query(stmt);
    if (!result)
        return 0;

    return result->Fetch()[0].GetUInt64();
}
