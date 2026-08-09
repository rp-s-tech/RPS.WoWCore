/*
 * NobleNext — GobBlueprint (AccountID templates + public/private + virtual center).
 *
 * External key: "$AccountID-$BlueprintID". Template-only mutations never touch world instances.
 * Root member may be virtual (entry=0): center pose without a GO.
 */

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include "QuaternionData.h"
#include "../GobGroup/noble_next_gobgroup_batch.h"
#include "../GobGroup/noble_next_gobgroup_mgr.h"
#include "../GobGroup/noble_next_gobgroup_transform.h"
#include "SharedDefines.h"

#include "DatabaseEnv.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Map;
class Player;

namespace RoleplayCore::NobleNext
{
    enum class GobBlueprintPartType : uint8
    {
        Base = 0,
        Object = 1,
        Group = 2
    };

    enum class GobBlueprintListScope : uint8
    {
        Mine = 0,
        Public = 1,
        All = 2
    };

    struct GobBlueprintMember
    {
        uint32 Id = 0;          // stable DB id
        uint32 PartId = 0;
        uint32 SortOrder = 0;
        bool IsRoot = false;
        uint32 Entry = 0;       // 0 + IsRoot => virtual center
        MemberRelativeTransform Relative;
    };

    struct GobBlueprintPart
    {
        uint32 Id = 0;
        GobBlueprintPartType Type = GobBlueprintPartType::Base;
        std::string Label;
        ObjectGuid::LowType SourceRootGuid = 0;
    };

    struct GobBlueprintRecord
    {
        uint32 Id = 0;
        uint32 OwnerAccountId = 0;
        std::string Name;
        std::string Description;
        bool IsPublic = false;
        bool Deleted = false;
        bool CanMutate = false;
        bool CanSetPublic = false;
        std::vector<GobBlueprintPart> Parts;
        std::vector<GobBlueprintMember> Members; // root first by sort_order

        std::string Key() const { return std::to_string(OwnerAccountId) + "-" + std::to_string(Id); }
        bool HasVirtualRoot() const
        {
            return !Members.empty() && Members.front().IsRoot && Members.front().Entry == 0;
        }
        uint32 PhysicalMemberCount() const
        {
            uint32 n = 0;
            for (GobBlueprintMember const& m : Members)
                if (m.Entry != 0)
                    ++n;
            return n;
        }
    };

    struct GobBlueprintListItem
    {
        uint32 Id = 0;
        uint32 OwnerAccountId = 0;
        std::string Name;
        uint32 MemberCount = 0;     // physical GO count
        bool IsPublic = false;
        bool CanMutate = false;
        std::string Description;

        std::string Key() const { return std::to_string(OwnerAccountId) + "-" + std::to_string(Id); }
    };

    class GobBlueprintMgr
    {
    public:
        static GobBlueprintMgr& Instance();

        void Update(uint32 diff);

        bool List(Player* player, GobBlueprintListScope scope, std::string const& filter,
            std::vector<GobBlueprintListItem>& out, std::string& error);
        bool Info(Player* player, std::string const& keyOrName, GobBlueprintRecord& out, std::string& error);
        bool NewFromActiveGroup(Player* player, std::string const& name, std::string& error);
        bool UpdateFromActiveGroup(Player* player, std::string const& keyOrName, std::string& error);
        bool Delete(Player* player, std::string const& keyOrName, std::string& error);
        bool Rename(Player* player, std::string const& keyOrName, std::string const& newName, std::string& error);
        bool SetPublic(Player* player, std::string const& keyOrName, bool isPublic, std::string& error);

        bool MemberAddObject(Player* player, std::string const& keyOrName,
            ObjectGuid::LowType anchorGroupGuid, ObjectGuid::LowType objectGuid, std::string& error);
        bool MemberAddGroup(Player* player, std::string const& keyOrName,
            ObjectGuid::LowType anchorGroupGuid, ObjectGuid::LowType groupGuid, std::string& error);
        bool MemberRemoveObject(Player* player, std::string const& keyOrName, uint32 memberId, std::string& error);
        bool MemberRemoveGroup(Player* player, std::string const& keyOrName, uint32 partId, std::string& error);
        bool MemberReplace(Player* player, std::string const& keyOrName, uint32 memberId, uint32 newEntry, std::string& error);
        bool MemberSetRoot(Player* player, std::string const& keyOrName, uint32 memberId, std::string& error);
        bool MemberSetCenter(Player* player, std::string const& keyOrName, bool fromPlayerPose, std::string& error);

        bool Spawn(Player* player, std::string const& keyOrName, std::string& error);
        std::string BuildStatus(Player* player) const;

    private:
        GobBlueprintMgr() = default;

        enum class SpawnPhase : uint8
        {
            None = 0,
            WorldPending,
            RoleplayPending,
            RuntimePending,
            Completed,
            Failed,
            Compensated
        };

        struct StagedSpawn
        {
            ObjectGuid::LowType SpawnId = 0;
            uint32 Entry = 0;
            Position WorldPos;
            QuaternionData WorldRot;
            MemberRelativeTransform RelativeToRoot;
            bool IsRoot = false;
        };

        struct SpawnJob
        {
            uint32 AccountId = 0;
            uint32 BnetAccountId = 0;
            ObjectGuid PlayerGuid;
            uint32 MapId = 0;
            Difficulty Difficulty = DIFFICULTY_NONE;
            uint64 PhaseId = 0;
            std::string BlueprintKey;
            std::string BlueprintName;
            std::string GroupName;
            std::vector<StagedSpawn> Staged;
            ObjectGuid::LowType RootGuid = 0;
            SpawnPhase Phase = SpawnPhase::None;
            Optional<TransactionCallback> WorldCallback;
            uint32 SqlChunks = 0;
            uint32 QueuedAtMs = 0;
            uint32 DbStartMs = 0;
            uint32 DbElapsedMs = 0;
            uint32 RuntimeElapsedMs = 0;
            GobGroupBatchTelemetry Telemetry;
            std::string Error;
        };

        uint32 GetAccountId(Player* player) const;
        uint32 GetBnetAccountId(Player* player) const;
        bool HasStaffOverride(Player* player) const;
        bool NormalizeName(std::string& name, std::string& error) const;
        bool ParseKey(std::string const& text, uint32& ownerAccountId, uint32& blueprintId) const;
        std::string FormatKey(uint32 ownerAccountId, uint32 blueprintId) const;

        bool CanDiscover(Player* player, GobBlueprintRecord const& bp) const;
        bool CanSpawn(Player* player, GobBlueprintRecord const& bp) const;
        bool CanMutate(Player* player, GobBlueprintRecord const& bp) const;
        bool CanSetPublic(Player* player, GobBlueprintRecord const& bp) const;

        bool ResolveBlueprint(Player* player, std::string const& keyOrName, bool forMutate, bool requirePhysical,
            GobBlueprintRecord& out, std::string& error) const;
        bool LoadBlueprintById(uint32 blueprintId, GobBlueprintRecord& out, std::string& error) const;
        bool LoadMembersAndParts(GobBlueprintRecord& out, std::string& error) const;
        void FillCapabilities(Player* player, GobBlueprintRecord& bp) const;
        void FillCapabilities(Player* player, GobBlueprintListItem& item) const;

        bool CaptureActiveGroup(Player* player, GobGroupRelativeSnapshot& snap, QuaternionData& rootTilt,
            std::string& error) const;
        bool ReplaceComposition(uint32 blueprintId, std::vector<GobBlueprintMember> const& members,
            ObjectGuid::LowType sourceRootGuid, std::string& error);
        bool PersistMembers(RoleplayDatabaseTransaction& trans, uint32 blueprintId, uint32 partId,
            std::vector<GobBlueprintMember> const& members);

        bool HasBusySpawn(uint32 accountId) const;
        bool StartWorldTransaction(SpawnJob& job);
        void ContinueSpawnAfterWorld(uint32 accountId);
        bool CommitRoleplay(SpawnJob& job);
        void CompensateWorld(SpawnJob& job);
        void NotifySpawnResult(SpawnJob& job, bool ok);
        void ScheduleRuntimePublish(SpawnJob& job);
        void ProcessRuntimePublish(Map* map, uint32 accountId);

        mutable std::mutex _mutex;
        std::unordered_map<uint32, std::unique_ptr<SpawnJob>> _spawnJobs;
    };
}

#define sGobBlueprintMgr RoleplayCore::NobleNext::GobBlueprintMgr::Instance()
