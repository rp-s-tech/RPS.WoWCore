#pragma once

#include "Define.h"
#include "Optional.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

class Player;

enum class RoleplayPhaseRole : uint8
{
    None = 0,
    Viewer,
    Editor,
    Manager,
    Owner
};

enum class RoleplayPhaseSpawnType : uint8
{
    Creature,
    GameObject
};

struct TC_GAME_API RoleplayPhaseMemberInfo
{
    uint64 CharacterGuid = 0;
    RoleplayPhaseRole Role = RoleplayPhaseRole::None;
};

struct TC_GAME_API RoleplayPhaseInfo
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
    std::vector<RoleplayPhaseMemberInfo> Members;

    bool HasSpawn() const
    {
        return SpawnMap && SpawnX && SpawnY && SpawnZ && SpawnO;
    }
};

struct TC_GAME_API RoleplayPhaseSpawnInfo
{
    uint64 PhaseId = 0;
    RoleplayPhaseSpawnType Type = RoleplayPhaseSpawnType::Creature;
    uint64 SpawnId = 0;
    uint32 MapId = 0;
};

struct TC_GAME_API RoleplayPhaseAddonData
{
    std::string Key;
    std::string Value;
    uint64 Version = 0;
    uint64 UpdatedBy = 0;
};

using RoleplayPhaseAddonMessageHandler = bool (*)(Player* player, uint32 chatType,
    std::string_view prefix, std::string_view message, bool selfWhisper);
using RoleplayPhaseTransitionHandler = void (*)(Player* player, uint64 previousPhaseId, uint64 currentPhaseId);

// Logical RP phase IDs are database BIGINT UNSIGNED values. They are not native Phase.db2 IDs.
class TC_GAME_API RoleplayPhaseMgr
{
public:
    static RoleplayPhaseMgr& Instance();

    // Loads the early snapshot before ObjectMgr has loaded persistent spawns.
    // Reload() performs the final spawn-orphan validation once ObjectMgr is ready.
    bool Load();
    bool Reload();

    // Hot-path ID lookups: immutable snapshot only; no SQL, mutex, or string construction.
    uint64 GetPlayerPhaseId(uint64 characterGuid) const;
    uint64 GetPlayerPhaseId(uint64 characterGuid, uint32 mapId) const;
    uint64 GetSpawnPhaseId(RoleplayPhaseSpawnType type, uint64 spawnId) const;
    // Map-scoped spawn lookup: returns 0 when the persistent mapping belongs to another map.
    uint64 GetSpawnPhaseId(RoleplayPhaseSpawnType type, uint64 spawnId, uint32 mapId) const;

    // Persistent world spawns retain their native phase data. These methods
    // assign only the separate logical RP phase mapping and publish it
    // atomically with its roleplay-database write.
    bool AssignPersistentSpawn(RoleplayPhaseSpawnType type, uint64 spawnId, uint32 mapId, uint64 phaseId);
    bool ClearPersistentSpawn(RoleplayPhaseSpawnType type, uint64 spawnId);
    // Batch variants use one roleplay DB transaction and publish one immutable
    // snapshot, so grouped gameobjects never become partially reassigned.
    bool AssignPersistentSpawns(RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds, uint64 phaseId);
    bool ClearPersistentSpawns(RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds);

    // v1 exclusive truth table: common(0)=common(0), A=A; common/A and A/B are false.
    // This is only the logical rule; package 14 owns visibility integration.
    static constexpr bool SharesExclusiveContext(uint64 leftPhaseId, uint64 rightPhaseId)
    {
        return leftPhaseId == rightPhaseId;
    }

    bool IsEnabled(uint64 phaseId) const;
    bool HasRole(uint64 phaseId, uint64 characterGuid, uint32 accountId, RoleplayPhaseRole required) const;
    // Membership/owner role without usability gate (archived phases still report a role).
    RoleplayPhaseRole GetMemberRole(uint64 phaseId, uint64 characterGuid, uint32 accountId) const;
    // Discovery policy: public usable phases, member/owner phases, or staff access for known phases.
    bool CanDiscover(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false) const;
    static constexpr bool CanDiscoverUsablePhase(bool isPublic, bool isUsable, bool hasViewerRole)
    {
        return isUsable && (isPublic || hasViewerRole);
    }
    // Compatibility alias for existing callers.
    bool CanView(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false) const;
    // Enter policy: usable + map scope + (public or member/owner or staffAccess).
    bool CanEnter(uint64 phaseId, uint64 characterGuid, uint32 accountId, uint32 mapId,
        bool staffAccess = false) const;
    // Server-owned phases (owner account 0) require GM1+ for every mutation.
    bool CanEdit(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false,
        bool serverStaff = false) const;
    bool CanManage(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false,
        bool serverStaff = false) const;
    // Owner-account (or staff): rename / public / transfer owner / archive. Manager is not enough.
    bool CanOwnPhase(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false,
        bool serverStaff = false) const;
    bool CanArchive(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false,
        bool serverStaff = false) const;
    // Archived phases are not CanOwnPhase-able via usable gate; unarchive uses Owner without usability gate.
    bool CanUnarchive(uint64 phaseId, uint64 characterGuid, uint32 accountId, bool staffAccess = false,
        bool serverStaff = false) const;
    // Owner account or any membership row (includes archived); used by `.rps phase list my`.
    bool IsOwnedOrMember(uint64 phaseId, uint64 characterGuid, uint32 accountId) const;
    // Strict account ownership (includes archived); used by LIST owned / Codex «Личные».
    bool IsOwnerAccount(uint64 phaseId, uint32 accountId) const;
    static bool CanMutateCommonWorld(uint32 securityLevel);
    uint64 GetAddonDataRevision(uint64 phaseId, std::string_view key) const;
    bool GetAddonData(uint64 phaseId, std::string_view key, RoleplayPhaseAddonData& data) const;
    void GetAddonDataByNamespace(uint64 phaseId, std::string_view nameSpace, std::vector<RoleplayPhaseAddonData>& data) const;
    bool GetPhaseInfo(uint64 phaseId, RoleplayPhaseInfo& info) const;
    void GetPhaseList(std::vector<RoleplayPhaseInfo>& phases) const;
    bool GetSpawnInfo(RoleplayPhaseSpawnType type, uint64 spawnId, RoleplayPhaseSpawnInfo& info) const;

    // Script modules register their narrow protocol handlers here. The chat
    // handler supplies the original addon prefix and self-whisper state before
    // forwarding the packet, so a client cannot spoof the routing context.
    void RegisterAddonMessageHandler(RoleplayPhaseAddonMessageHandler handler);
    bool HandleAddonMessage(Player* player, uint32 chatType, std::string_view prefix,
        std::string_view message, bool selfWhisper) const;
    void RegisterTransitionHandler(RoleplayPhaseTransitionHandler handler);

    // Command mutation entry points validate their own phase ACL. Audit records
    // are intentionally supplied by the command layer, which owns RBAC/bypass context.
    bool Create(std::string const& name, std::string const& description, uint32 ownerAccountId,
        uint64 ownerCharacterGuid, uint64& phaseId, Optional<uint32> mapId = {});
    // staffBypass allows RBAC 3045 archive after an explicit confirm at the call site.
    bool Archive(uint64 phaseId, uint64 actorCharacterGuid, uint32 actorAccountId, bool staffBypass = false,
        bool serverStaff = false);
    bool Unarchive(uint64 phaseId, uint64 actorCharacterGuid, uint32 actorAccountId, bool staffBypass = false,
        bool serverStaff = false);
    bool RemoveMember(uint64 phaseId, uint64 characterGuid, uint64 actorCharacterGuid, uint32 actorAccountId,
        bool staffAccess = false, bool serverStaff = false);
    bool AssignSpawn(uint64 phaseId, RoleplayPhaseSpawnType type, uint64 spawnId,
        uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess = false, bool serverStaff = false);
    bool AssignSpawns(uint64 phaseId, RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds,
        uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess = false, bool serverStaff = false);
    bool ClearSpawn(RoleplayPhaseSpawnType type, uint64 spawnId, uint64 actorCharacterGuid, uint32 actorAccountId,
        bool staffAccess = false, bool serverStaff = false);
    bool ClearSpawns(RoleplayPhaseSpawnType type, std::vector<uint64> const& spawnIds,
        uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess = false, bool serverStaff = false);
    bool SetPublic(uint64 phaseId, bool isPublic, uint64 actorCharacterGuid, uint32 actorAccountId,
        bool staffAccess = false, bool serverStaff = false);
    bool Rename(uint64 phaseId, std::string const& name, uint64 actorCharacterGuid, uint32 actorAccountId,
        bool staffAccess = false, bool serverStaff = false);
    bool SetOwner(uint64 phaseId, uint32 newOwnerAccountId, uint64 managerCharacterGuid,
        uint64 actorCharacterGuid, uint32 actorAccountId, bool force, bool staffAccess = false, bool serverStaff = false);
    bool SetMemberRole(uint64 phaseId, uint64 characterGuid, RoleplayPhaseRole role,
        uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess = false, bool serverStaff = false);
    bool SetEnterSpawnPoint(uint64 phaseId, uint32 mapId, float x, float y, float z, float o,
        uint64 actorCharacterGuid, uint32 actorAccountId, bool staffAccess = false, bool serverStaff = false);
    bool SetEnterSpawnEnabled(uint64 phaseId, bool enabled, uint64 actorCharacterGuid, uint32 actorAccountId,
        bool staffAccess = false, bool serverStaff = false);

    // The sole live-player mutation path. It validates and persists the new
    // logical context before atomically publishing its snapshot entry.
    bool TransitionPlayer(Player* player, uint64 targetPhaseId, Optional<uint32> validationMapId = {});
    // Transitions to a phase and teleports to its configured enter spawn. Rolls
    // back the logical context if the teleport is rejected.
    bool GotoPhase(Player* player, uint64 phaseId);

    // Restores the persisted context before Map::AddPlayerToMap. Invalid
    // persisted rows are cleared through TransitionPlayer and audited.
    bool RestorePlayerContext(Player* player);

    // Compatibility entry point for offline characters. Live players are
    // redirected through TransitionPlayer.
    bool SetActive(uint64 characterGuid, uint64 phaseId, uint32 accountId, uint32 mapId);
    bool AddMember(uint64 phaseId, uint64 characterGuid, RoleplayPhaseRole role,
        uint64 actorCharacterGuid, uint32 actorAccountId);

    void WriteAudit(uint64 actorCharacterGuid, uint32 actorAccountId, std::string_view action,
        uint64 phaseId, std::string_view detail) const;

private:
    struct Snapshot;
    struct LoadSummary;

    bool ReloadInternal(bool validateSpawns);
    bool BuildSnapshot(std::shared_ptr<Snapshot>& snapshot, LoadSummary& summary, bool validateSpawns) const;
    std::shared_ptr<Snapshot const> GetSnapshot() const;
    static bool IsUsablePhase(Snapshot const& snapshot, uint64 phaseId);
    static bool MapScopeAllows(Snapshot const& snapshot, uint64 phaseId, uint32 mapId);
    static bool HasPhaseSpawn(Snapshot const& snapshot, uint64 phaseId);
    bool VerifyPhaseMetadata(uint64 phaseId, RoleplayPhaseInfo const& expected) const;
    bool WriteActivePhase(uint64 characterGuid, uint32 accountId, uint64 phaseId) const;
    bool VerifyActivePhase(uint64 characterGuid, uint64 phaseId) const;
    static bool BuildActivePhaseSnapshot(std::shared_ptr<Snapshot>& snapshot, std::shared_ptr<Snapshot const> const& current,
        uint64 characterGuid, uint64 phaseId);
    bool TransitionCharacter(uint64 characterGuid, uint64 phaseId, uint32 accountId, uint32 mapId);
    void WriteTransitionAudit(uint64 characterGuid, uint32 accountId, std::string_view action, uint64 phaseId,
        std::string_view detail) const;

    struct ContextReconciliation
    {
        uint64 CharacterGuid = 0;
        uint64 PreviousPhaseId = 0;
        uint64 CurrentPhaseId = 0;
        bool ClearPersisted = false;
        std::string AuditAction;
    };

    // Collects online/offline contexts that silently disappeared after a snapshot
    // publish (archive / member revoke / reload quarantine). Callers apply
    // effects only after releasing _mutationMutex.
    void CollectContextReconciliations(std::shared_ptr<Snapshot const> const& previous,
        std::shared_ptr<Snapshot const> const& current, std::vector<ContextReconciliation>& out) const;
    void ApplyContextReconciliations(std::vector<ContextReconciliation> const& reconciliations);
    bool VerifyPersistentSpawnMapping(RoleplayPhaseSpawnType type, uint64 spawnId, uint64 expectedPhaseId,
        uint32 expectedMapId) const;
    bool VerifyPersistentSpawnCleared(RoleplayPhaseSpawnType type, uint64 spawnId) const;
    uint64 QueryLatestPhaseIdByMeta(std::string const& name, std::string const& description,
        uint32 ownerAccountId, Optional<uint32> mapId) const;

    std::shared_ptr<Snapshot> _snapshot;
    mutable std::mutex _mutationMutex;
    RoleplayPhaseAddonMessageHandler _addonMessageHandler = nullptr;
    RoleplayPhaseTransitionHandler _transitionHandler = nullptr;
};

#define sRoleplayPhaseMgr RoleplayPhaseMgr::Instance()
