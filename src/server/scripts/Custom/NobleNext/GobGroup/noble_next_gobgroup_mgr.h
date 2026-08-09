/*
 * NobleNext — spatial GameObject soft groups manager.
 */

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include "noble_next_gobgroup_transform.h"

#include "DatabaseEnv.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Map;
class Player;
struct GameObjectData;

namespace RoleplayCore::NobleNext
{
    struct GobGroupObjectSnapshot
    {
        ObjectGuid::LowType Guid = 0;
        uint32 Entry = 0;
        uint32 MapId = 0;
        uint64 LogicalPhaseId = 0;
        float X = 0.f;
        float Y = 0.f;
        float Z = 0.f;
        float O = 0.f;
        bool InGroup = false;
        bool IsRoot = false;
    };

    struct GobGroupInfoSnapshot
    {
        ObjectGuid::LowType ObjectGuid = 0;
        ObjectGuid::LowType GroupRoot = 0; // 0 = not in a group
        std::string Name;
        uint32 MemberCount = 0;
        bool Dirty = false;
        bool Busy = false;
        GobGroupObjectSnapshot Object;
        std::vector<GobGroupObjectSnapshot> Members;
    };

    struct GobGroupListItemSnapshot
    {
        ObjectGuid::LowType RootGuid = 0;
        std::string Name;
        uint32 MemberCount = 0;
        uint32 MapId = 0;
        uint64 LogicalPhaseId = 0;
    };

    struct GobGroupListSnapshot
    {
        Optional<uint32> MapFilter;
        std::vector<GobGroupListItemSnapshot> Items;
    };

    struct GobGroupNearItemSnapshot
    {
        ObjectGuid::LowType RootGuid = 0;
        std::string Name;
        uint32 MemberCount = 0;
        uint32 MapId = 0;
        uint64 LogicalPhaseId = 0;
        float Distance = 0.f;
        float X = 0.f;
        float Y = 0.f;
        float Z = 0.f;
    };

    struct GobGroupNearSnapshot
    {
        uint32 MapId = 0;
        float Radius = 0.f;
        std::vector<GobGroupNearItemSnapshot> Items;
    };

    struct GobGroupStatusSnapshot
    {
        ObjectGuid::LowType GroupGuid = 0;
        bool Dirty = false;
        bool Busy = false;
        std::string JobState; // None|Queued|Calculated|DbPending|RuntimePending|Completed|Failed
    };

    class GobGroupMgr
    {
    public:
        static GobGroupMgr& Instance();

        void LoadAndValidate();
        bool Reload(std::string& error);
        void Update(uint32 diff); // poll TransactionCallback

        // lookups — anyGuid = root or member of a soft group
        ObjectGuid::LowType FindRootGuid(ObjectGuid::LowType anyGuid) const;
        bool IsRoot(ObjectGuid::LowType guid) const;
        bool IsMember(ObjectGuid::LowType guid) const;
        bool IsBusy(ObjectGuid::LowType anyGuid) const;
        // Returns root + members for an existing group. Used by logical phase
        // assignment to keep a spatial group in one context atomically.
        bool TryGetGroupSpawnIds(ObjectGuid::LowType anyGuid, std::vector<uint64>& outSpawnIds) const;

        // active group per account (UI context only; mutations take explicit GUID)
        void SetActiveRoot(uint32 accountId, ObjectGuid::LowType anyGuid);
        ObjectGuid::LowType GetActiveRoot(uint32 accountId) const;

        // CRUD / metadata — groupGuid resolves root|member → canonical root
        bool Create(Player* player, ObjectGuid::LowType rootGuid, std::string const& name, std::string& error);
        bool AddMember(ObjectGuid::LowType groupGuid, ObjectGuid::LowType memberGuid, std::string& error);
        bool RemoveMember(ObjectGuid::LowType groupGuid, ObjectGuid::LowType memberGuid, std::string& error);
        bool Dissolve(ObjectGuid::LowType groupGuid, std::string& error);
        bool DeleteFull(ObjectGuid::LowType groupGuid, bool fullForce, std::string& report);
        bool CleanupOrphans(bool confirm, std::string& report);

        // info accepts any existing GO; grouped roots/members include canonical group details
        std::string BuildInfo(Player const* viewer, ObjectGuid::LowType objectGuid) const;
        std::string BuildCheck(ObjectGuid::LowType anyGuid) const;
        std::string BuildStatus(ObjectGuid::LowType anyGuid) const;
        std::string BuildList(Player const* viewer, Optional<uint32> mapId) const;

        // structured snapshots for NN_GOBGROUP addon clients
        bool TryGetInfoSnapshot(Player const* viewer, ObjectGuid::LowType objectGuid, GobGroupInfoSnapshot& out, std::string& error) const;
        void GetListSnapshot(Player const* viewer, Optional<uint32> mapId, GobGroupListSnapshot& out) const;
        void GetNearSnapshot(Player const* player, float radius, GobGroupNearSnapshot& out) const;
        bool TryGetStatusSnapshot(ObjectGuid::LowType anyGuid, GobGroupStatusSnapshot& out, std::string& error) const;
        uint32 ScanNear(Player* player, ObjectGuid::LowType groupGuid, float radius,
            std::vector<ObjectGuid::LowType>& outCandidates, std::string& report) const;
        bool AddNear(Player* player, ObjectGuid::LowType groupGuid, float radius, bool confirm, std::string& report);

        // relative sync
        bool Capture(ObjectGuid::LowType anyGuid, bool silent, std::string& error); // member or root
        bool Recalc(ObjectGuid::LowType groupGuid, std::string& error);
        bool Sync(ObjectGuid::LowType groupGuid, std::string& error); // apply relative -> absolute batch

        // after vanilla .gobject move/turn SaveToDB
        void OnSingleObjectTransformSaved(ObjectGuid::LowType spawnId);
        bool CanDeleteGameObject(ObjectGuid::LowType spawnId, std::string& error) const;
        void OnGameObjectDeleted(ObjectGuid::LowType spawnId);

        // group transforms (async jobs)
        bool Move(ObjectGuid::LowType groupGuid, Position const& newRootPos, std::string& error);
        bool Turn(ObjectGuid::LowType groupGuid, float newOrientation, std::string& error);
        bool Nudge(ObjectGuid::LowType groupGuid, float dx, float dy, float dz, std::string& error);
        bool RotateDelta(ObjectGuid::LowType groupGuid, float dYawRad, float dPitchRad, float dRollRad, std::string& error);
        bool ScaleUniform(ObjectGuid::LowType groupGuid, float factor, std::string& error);
        bool Relocate(ObjectGuid::LowType groupGuid, uint32 mapId, Position const& newRootPos, bool confirm, std::string& error);

    private:
        GobGroupMgr() = default;

        enum class JobPhase : uint8
        {
            None = 0,
            Queued,
            Calculated,
            DbPending,
            RuntimePending,
            Completed,
            Failed
        };

        enum class RuntimeStage : uint8
        {
            Hide = 0,
            Apply,
            Reveal,
            Done
        };

        struct GroupRecord
        {
            ObjectGuid::LowType RootGuid = 0;
            std::string Name;
            uint32 CreatedBy = 0;
            std::vector<ObjectGuid::LowType> Members; // sorted ASC
            std::unordered_map<ObjectGuid::LowType, MemberRelativeTransform> Relatives;
            bool Dirty = false;
            std::string DirtyReason;
            std::string LastError;
        };

        struct GroupJob
        {
            ObjectGuid::LowType RootGuid = 0;
            JobPhase Phase = JobPhase::None;
            RuntimeStage Stage = RuntimeStage::Hide;
            GroupTransformPlan Plan;
            std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> PendingRelatives;
            std::vector<std::pair<ObjectGuid::LowType, float>> PendingSizes;
            bool ClearDirtyOnSuccess = false;
            Optional<TransactionCallback> DbCallback;
            size_t RuntimeIndex = 0;
            uint32 SqlChunks = 0;
            uint32 RuntimeChunks = 0;
            std::string Error;
            bool UpdateMap = false;
        };

        mutable std::mutex _mutex;
        std::unordered_map<ObjectGuid::LowType, GroupRecord> _groups;
        std::unordered_map<ObjectGuid::LowType, ObjectGuid::LowType> _memberToRoot;
        std::unordered_map<uint32, ObjectGuid::LowType> _activeRootByAccount;
        std::unordered_map<ObjectGuid::LowType, std::unique_ptr<GroupJob>> _jobs;

        struct ResolvedGroup
        {
            ObjectGuid::LowType InputGuid = 0;
            ObjectGuid::LowType RootGuid = 0;
            bool IsRoot = false;
        };

        GroupRecord* FindGroup(ObjectGuid::LowType rootGuid);
        GroupRecord const* FindGroup(ObjectGuid::LowType rootGuid) const;
        bool IsBusyUnlocked(ObjectGuid::LowType rootGuid) const;
        bool HasBusyJobsUnlocked() const;

        // Caller already holds _mutex.
        ObjectGuid::LowType FindRootGuidUnlocked(ObjectGuid::LowType anyGuid) const;
        bool TryResolveGroupUnlocked(ObjectGuid::LowType anyGuid, ResolvedGroup& out) const;
        bool RequireGroupUnlocked(ObjectGuid::LowType anyGuid, ResolvedGroup& out, std::string& error) const;

        static bool IsUnsupportedGo(GameObjectData const* data, std::string& reason);
        static bool IsEventManaged(ObjectGuid::LowType spawnId);
        static uint64 GetLogicalPhaseId(ObjectGuid::LowType spawnId);
        static uint64 GetViewerPhaseId(Player const* viewer);
        static bool IsVisibleInLogicalContext(Player const* viewer, ObjectGuid::LowType spawnId);
        bool ValidateSpawnForGroup(ObjectGuid::LowType spawnId, ObjectGuid::LowType expectedMapRoot,
            bool asRoot, std::string& error) const;
        bool ValidateGroupIntegrity(GroupRecord const& group, std::string& error) const;
        bool CheckRelativeConsistency(GroupRecord const& group, std::string& error) const;

        void MarkDirty(GroupRecord& group, std::string const& reason);
        void ClearDirty(GroupRecord& group);

        bool ComputeRelativeForMember(ObjectGuid::LowType rootGuid, ObjectGuid::LowType memberGuid,
            MemberRelativeTransform& out, std::string& error) const;
        bool PersistRelativeRows(ObjectGuid::LowType rootGuid,
            std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> const& rows, std::string& error);
        bool PersistSingleRelative(ObjectGuid::LowType rootGuid, ObjectGuid::LowType memberGuid,
            MemberRelativeTransform const& rel, std::string& error);

        bool CollectSnapshot(GroupRecord const& group,
            std::unordered_map<ObjectGuid::LowType, Position>& positions,
            std::unordered_map<ObjectGuid::LowType, QuaternionData>& rotations,
            uint32& mapId, std::string& error) const;

        bool EnqueueTransform(ObjectGuid::LowType rootGuid, Position const& newRootPos,
            QuaternionData const& newRootRotation, uint32 targetMapId, bool updateMap,
            bool requireRelativeConsistency, std::string& error);
        bool StartJobDb(GroupJob& job);
        void ProcessRuntime(Map* map, ObjectGuid::LowType rootGuid);
        void ScheduleRuntime(GroupJob& job);
        void CompleteJob(ObjectGuid::LowType rootGuid, bool success, std::string const& error);
        char const* PhaseName(JobPhase phase) const;

        void UnindexGroup(GroupRecord const& group);
        void IndexGroup(GroupRecord const& group);
    };

}

#define sGobGroupMgr RoleplayCore::NobleNext::GobGroupMgr::Instance()
