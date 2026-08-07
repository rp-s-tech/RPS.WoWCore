/*
 * NobleNext — spatial GameObject soft groups manager.
 */

#include "noble_next_gobgroup_mgr.h"

#include "DB2Stores.h"
#include "GameEventMgr.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "GridDefines.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Util.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace RoleplayCore::NobleNext
{
namespace
{
    Position const& SpawnPosition(GameObjectData const& data)
    {
        return data.spawnPoint;
    }
}

GobGroupMgr& GobGroupMgr::Instance()
{
    static GobGroupMgr instance;
    return instance;
}

GobGroupMgr::GroupRecord* GobGroupMgr::FindGroup(ObjectGuid::LowType rootGuid)
{
    auto it = _groups.find(rootGuid);
    return it != _groups.end() ? &it->second : nullptr;
}

GobGroupMgr::GroupRecord const* GobGroupMgr::FindGroup(ObjectGuid::LowType rootGuid) const
{
    auto it = _groups.find(rootGuid);
    return it != _groups.end() ? &it->second : nullptr;
}

void GobGroupMgr::IndexGroup(GroupRecord const& group)
{
    for (ObjectGuid::LowType member : group.Members)
        _memberToRoot[member] = group.RootGuid;
}

void GobGroupMgr::UnindexGroup(GroupRecord const& group)
{
    for (ObjectGuid::LowType member : group.Members)
        _memberToRoot.erase(member);
}

bool GobGroupMgr::IsEventManaged(ObjectGuid::LowType spawnId)
{
    for (auto const& guidList : sGameEventMgr->mGameEventGameobjectGuids)
        for (ObjectGuid::LowType guid : guidList)
            if (guid == spawnId)
                return true;
    return false;
}

bool GobGroupMgr::IsUnsupportedGo(GameObjectData const* data, std::string& reason)
{
    if (!data)
    {
        reason = "spawn missing in ObjectMgr";
        return true;
    }

    GameObjectTemplate const* proto = sObjectMgr->GetGameObjectTemplate(data->id);
    if (!proto)
    {
        reason = Trinity::StringFormat("missing gameobject_template {}", data->id);
        return true;
    }

    if (proto->type == GAMEOBJECT_TYPE_TRANSPORT || proto->type == GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT)
    {
        reason = "transport / MOTransport not allowed";
        return true;
    }
    if (sObjectMgr->IsTransportMap(data->mapId))
    {
        reason = "spawn is on a transport map";
        return true;
    }

    if (data->poolId)
    {
        reason = Trinity::StringFormat("pooled GO (pool {})", data->poolId);
        return true;
    }

    if (IsEventManaged(data->spawnId))
    {
        reason = "game_event managed GO";
        return true;
    }

    return false;
}

bool GobGroupMgr::ValidateSpawnForGroup(ObjectGuid::LowType spawnId, ObjectGuid::LowType expectedMapRoot,
    bool asRoot, std::string& error) const
{
    GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnId);
    std::string reason;
    if (IsUnsupportedGo(data, reason))
    {
        error = Trinity::StringFormat("GO {} unsupported: {}", spawnId, reason);
        return false;
    }

    if (asRoot)
    {
        if (_memberToRoot.contains(spawnId))
        {
            error = Trinity::StringFormat("GO {} is already a member of group {}", spawnId, _memberToRoot.at(spawnId));
            return false;
        }
        if (_groups.contains(spawnId))
        {
            error = Trinity::StringFormat("GO {} is already a group root", spawnId);
            return false;
        }
    }
    else
    {
        if (_groups.contains(spawnId))
        {
            error = Trinity::StringFormat("GO {} is a group root and cannot be a member", spawnId);
            return false;
        }
        if (_memberToRoot.contains(spawnId))
        {
            error = Trinity::StringFormat("GO {} already belongs to group {}", spawnId, _memberToRoot.at(spawnId));
            return false;
        }

        GameObjectData const* rootData = sObjectMgr->GetGameObjectData(expectedMapRoot);
        if (!rootData || rootData->mapId != data->mapId)
        {
            error = Trinity::StringFormat("GO {} is not on the same map as root {}", spawnId, expectedMapRoot);
            return false;
        }
    }

    return true;
}

bool GobGroupMgr::ValidateGroupIntegrity(GroupRecord const& group, std::string& error) const
{
    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(group.RootGuid);
    std::string reason;
    if (IsUnsupportedGo(rootData, reason))
    {
        error = Trinity::StringFormat("root {}: {}", group.RootGuid, reason);
        return false;
    }

    if (group.Members.size() > GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("root {}: too many members ({})", group.RootGuid, group.Members.size());
        return false;
    }

    for (ObjectGuid::LowType member : group.Members)
    {
        if (member == group.RootGuid)
        {
            error = Trinity::StringFormat("root {}: root listed as member", group.RootGuid);
            return false;
        }

        GameObjectData const* memberData = sObjectMgr->GetGameObjectData(member);
        if (IsUnsupportedGo(memberData, reason))
        {
            error = Trinity::StringFormat("member {}: {}", member, reason);
            return false;
        }

        if (memberData->mapId != rootData->mapId)
        {
            error = Trinity::StringFormat("member {} map {} != root map {}", member, memberData->mapId, rootData->mapId);
            return false;
        }

        auto relIt = group.Relatives.find(member);
        if (relIt == group.Relatives.end()
            || !GobGroupTransform::IsFinite(Position(relIt->second.OffsetX, relIt->second.OffsetY, relIt->second.OffsetZ, relIt->second.OffsetO))
            || !GobGroupTransform::IsFinite(relIt->second.RelativeRotation))
        {
            error = Trinity::StringFormat("member {}: invalid relative transform", member);
            return false;
        }
    }

    return true;
}

bool GobGroupMgr::CheckRelativeConsistency(GroupRecord const& group, std::string& error) const
{
    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(group.RootGuid);
    if (!rootData)
    {
        error = "root spawn missing";
        return false;
    }

    Position const rootPos = SpawnPosition(*rootData);
    for (ObjectGuid::LowType member : group.Members)
    {
        GameObjectData const* memberData = sObjectMgr->GetGameObjectData(member);
        if (!memberData)
        {
            error = Trinity::StringFormat("member {} missing", member);
            return false;
        }

        auto relIt = group.Relatives.find(member);
        if (relIt == group.Relatives.end())
        {
            error = Trinity::StringFormat("member {} has no relative row", member);
            return false;
        }

        Position const expected = GobGroupTransform::ApplyLocalOffsetDouble(rootPos, relIt->second);
        Position const actual = SpawnPosition(*memberData);
        if (!GobGroupTransform::PosNearlyEqual(expected, actual))
        {
            error = Trinity::StringFormat("member {} world pose drifts from saved relative transform", member);
            return false;
        }

        QuaternionData const expectedRot = GobGroupTransform::ApplyRelativeRotation(relIt->second.RelativeRotation, rootPos.GetOrientation());
        if (!GobGroupTransform::QuatNearlyEqual(expectedRot, memberData->rotation))
        {
            error = Trinity::StringFormat("member {} rotation drifts from saved relative quaternion", member);
            return false;
        }
    }

    return true;
}

void GobGroupMgr::MarkDirty(GroupRecord& group, std::string const& reason)
{
    group.Dirty = true;
    group.DirtyReason = reason;
    group.LastError = reason;
    TC_LOG_ERROR("scripts.noblenext.gobgroup", "Group {} marked dirty: {}", group.RootGuid, reason);
}

void GobGroupMgr::ClearDirty(GroupRecord& group)
{
    group.Dirty = false;
    group.DirtyReason.clear();
}

void GobGroupMgr::LoadAndValidate()
{
    std::scoped_lock lock(_mutex);

    _groups.clear();
    _memberToRoot.clear();
    _jobs.clear();

    struct RawMember
    {
        ObjectGuid::LowType MemberGuid = 0;
        ObjectGuid::LowType RootGuid = 0;
        MemberRelativeTransform Rel;
    };

    std::unordered_map<ObjectGuid::LowType, GroupRecord> pending;
    std::vector<RawMember> rawMembers;
    std::vector<ObjectGuid::LowType> orphanRoots;
    std::vector<ObjectGuid::LowType> orphanMembers;

    if (QueryResult rootResult = WorldDatabase.Query(
        "SELECT r.root_guid, r.name, r.created_by, g.guid "
        "FROM gameobject_group_root r LEFT JOIN gameobject g ON g.guid = r.root_guid"))
    {
        do
        {
            Field* fields = rootResult->Fetch();
            GroupRecord group;
            group.RootGuid = fields[0].GetUInt64();
            group.Name = fields[1].GetString();
            group.CreatedBy = fields[2].GetUInt32();

            if (fields[3].IsNull())
            {
                orphanRoots.push_back(group.RootGuid);
                continue;
            }

            pending.emplace(group.RootGuid, std::move(group));
        } while (rootResult->NextRow());
    }

    if (QueryResult memberResult = WorldDatabase.Query(
        "SELECT gg.member_guid, gg.root_guid, gg.offset_x, gg.offset_y, gg.offset_z, gg.offset_o, "
        "gg.rotation0, gg.rotation1, gg.rotation2, gg.rotation3, member_go.guid, root_go.guid "
        "FROM gameobject_group gg "
        "LEFT JOIN gameobject member_go ON member_go.guid = gg.member_guid "
        "LEFT JOIN gameobject root_go ON root_go.guid = gg.root_guid"))
    {
        do
        {
            Field* fields = memberResult->Fetch();
            RawMember raw;
            raw.MemberGuid = fields[0].GetUInt64();
            raw.RootGuid = fields[1].GetUInt64();
            raw.Rel.OffsetX = fields[2].GetFloat();
            raw.Rel.OffsetY = fields[3].GetFloat();
            raw.Rel.OffsetZ = fields[4].GetFloat();
            raw.Rel.OffsetO = fields[5].GetFloat();
            raw.Rel.RelativeRotation = GobGroupTransform::Unitize(QuaternionData(
                fields[6].GetFloat(), fields[7].GetFloat(), fields[8].GetFloat(), fields[9].GetFloat()));

            if (fields[10].IsNull() || fields[11].IsNull())
            {
                orphanMembers.push_back(raw.MemberGuid);
                continue;
            }

            rawMembers.push_back(raw);
        } while (memberResult->NextRow());
    }

    std::unordered_map<ObjectGuid::LowType, ObjectGuid::LowType> tempMemberIndex;
    for (RawMember const& raw : rawMembers)
    {
        auto groupIt = pending.find(raw.RootGuid);
        if (groupIt == pending.end())
        {
            orphanMembers.push_back(raw.MemberGuid);
            continue;
        }

        if (!sObjectMgr->GetGameObjectData(raw.MemberGuid))
        {
            orphanMembers.push_back(raw.MemberGuid);
            continue;
        }

        if (tempMemberIndex.contains(raw.MemberGuid))
        {
            TC_LOG_ERROR("scripts.noblenext.gobgroup",
                "Fail-closed: member {} duplicated (roots {} / {})",
                raw.MemberGuid, tempMemberIndex[raw.MemberGuid], raw.RootGuid);
            groupIt->second.Dirty = true;
            groupIt->second.DirtyReason = "duplicate member metadata";
            continue;
        }

        tempMemberIndex[raw.MemberGuid] = raw.RootGuid;
        groupIt->second.Members.push_back(raw.MemberGuid);
        groupIt->second.Relatives.emplace(raw.MemberGuid, raw.Rel);
    }

    if (!orphanRoots.empty() || !orphanMembers.empty())
    {
        std::sort(orphanRoots.begin(), orphanRoots.end());
        orphanRoots.erase(std::unique(orphanRoots.begin(), orphanRoots.end()), orphanRoots.end());
        std::sort(orphanMembers.begin(), orphanMembers.end());
        orphanMembers.erase(std::unique(orphanMembers.begin(), orphanMembers.end()), orphanMembers.end());

        WorldDatabaseTransaction cleanup = WorldDatabase.BeginTransaction();
        for (ObjectGuid::LowType rootGuid : orphanRoots)
        {
            cleanup->PAppend("DELETE FROM gameobject_group WHERE root_guid = {}", rootGuid);
            cleanup->PAppend("DELETE FROM gameobject_group_root WHERE root_guid = {}", rootGuid);
        }
        for (ObjectGuid::LowType memberGuid : orphanMembers)
            cleanup->PAppend("DELETE FROM gameobject_group WHERE member_guid = {}", memberGuid);
        WorldDatabase.DirectCommitTransaction(cleanup);

        TC_LOG_WARN("scripts.noblenext.gobgroup",
            "Removed orphan metadata for {} deleted roots and {} deleted/mislinked members",
            orphanRoots.size(), orphanMembers.size());
    }

    std::unordered_map<ObjectGuid::LowType, bool> invalidRoots;
    for (auto const& [rootGuid, group] : pending)
    {
        auto memberIt = tempMemberIndex.find(rootGuid);
        if (memberIt == tempMemberIndex.end())
            continue;

        invalidRoots[rootGuid] = true;
        invalidRoots[memberIt->second] = true;
        TC_LOG_ERROR("scripts.noblenext.gobgroup",
            "Fail-closed: GO {} is both root and member of group {}; both groups rejected",
            rootGuid, memberIt->second);
    }

    uint32 loaded = 0;
    uint32 rejected = 0;
    for (auto& [rootGuid, group] : pending)
    {
        if (invalidRoots.contains(rootGuid))
        {
            ++rejected;
            continue;
        }

        std::sort(group.Members.begin(), group.Members.end());
        std::string error;
        if (!ValidateGroupIntegrity(group, error))
        {
            TC_LOG_ERROR("scripts.noblenext.gobgroup", "Fail-closed load reject for root {}: {}", rootGuid, error);
            ++rejected;
            continue;
        }

        _groups.emplace(rootGuid, std::move(group));
        IndexGroup(_groups[rootGuid]);
        ++loaded;
    }

    for (auto it = _activeRootByAccount.begin(); it != _activeRootByAccount.end();)
    {
        if (!_groups.contains(it->second))
            it = _activeRootByAccount.erase(it);
        else
            ++it;
    }

    TC_LOG_INFO("scripts.noblenext.gobgroup",
        "Loaded {} gameobject groups ({} rejected, {} member rows scanned)",
        loaded, rejected, uint32(rawMembers.size()));
}

bool GobGroupMgr::Reload(std::string& error)
{
    {
        std::scoped_lock lock(_mutex);
        if (HasBusyJobsUnlocked())
        {
            error = "Cannot reload GobGroup cache while a transform job is active";
            return false;
        }
    }

    LoadAndValidate();
    return true;
}

ObjectGuid::LowType GobGroupMgr::FindRootGuidUnlocked(ObjectGuid::LowType anyGuid) const
{
    if (!anyGuid)
        return 0;
    if (_groups.contains(anyGuid))
        return anyGuid;
    auto it = _memberToRoot.find(anyGuid);
    return it != _memberToRoot.end() ? it->second : 0;
}

bool GobGroupMgr::TryResolveGroupUnlocked(ObjectGuid::LowType anyGuid, ResolvedGroup& out) const
{
    out = ResolvedGroup{};
    out.InputGuid = anyGuid;
    if (!anyGuid)
        return false;

    if (_groups.contains(anyGuid))
    {
        out.RootGuid = anyGuid;
        out.IsRoot = true;
        return true;
    }

    auto it = _memberToRoot.find(anyGuid);
    if (it == _memberToRoot.end() || !_groups.contains(it->second))
        return false;

    out.RootGuid = it->second;
    out.IsRoot = false;
    return true;
}

bool GobGroupMgr::RequireGroupUnlocked(ObjectGuid::LowType anyGuid, ResolvedGroup& out, std::string& error) const
{
    if (!TryResolveGroupUnlocked(anyGuid, out))
    {
        error = Trinity::StringFormat("GO {} is not part of any group", anyGuid);
        return false;
    }
    return true;
}

ObjectGuid::LowType GobGroupMgr::FindRootGuid(ObjectGuid::LowType anyGuid) const
{
    std::scoped_lock lock(_mutex);
    return FindRootGuidUnlocked(anyGuid);
}

bool GobGroupMgr::IsRoot(ObjectGuid::LowType guid) const
{
    std::scoped_lock lock(_mutex);
    return _groups.contains(guid);
}

bool GobGroupMgr::IsMember(ObjectGuid::LowType guid) const
{
    std::scoped_lock lock(_mutex);
    return _memberToRoot.contains(guid);
}

bool GobGroupMgr::IsBusyUnlocked(ObjectGuid::LowType rootGuid) const
{
    auto it = _jobs.find(rootGuid);
    if (it == _jobs.end() || !it->second)
        return false;
    JobPhase const phase = it->second->Phase;
    return phase == JobPhase::Queued || phase == JobPhase::Calculated
        || phase == JobPhase::DbPending || phase == JobPhase::RuntimePending;
}

bool GobGroupMgr::HasBusyJobsUnlocked() const
{
    return std::any_of(_jobs.begin(), _jobs.end(), [this](auto const& entry)
    {
        return IsBusyUnlocked(entry.first);
    });
}

bool GobGroupMgr::IsBusy(ObjectGuid::LowType anyGuid) const
{
    std::scoped_lock lock(_mutex);
    ObjectGuid::LowType const rootGuid = FindRootGuidUnlocked(anyGuid);
    return rootGuid && IsBusyUnlocked(rootGuid);
}

void GobGroupMgr::SetActiveRoot(uint32 accountId, ObjectGuid::LowType anyGuid)
{
    std::scoped_lock lock(_mutex);
    if (!anyGuid)
    {
        _activeRootByAccount.erase(accountId);
        return;
    }

    ObjectGuid::LowType const rootGuid = FindRootGuidUnlocked(anyGuid);
    if (!rootGuid || !_groups.contains(rootGuid))
        _activeRootByAccount.erase(accountId);
    else
        _activeRootByAccount[accountId] = rootGuid;
}

ObjectGuid::LowType GobGroupMgr::GetActiveRoot(uint32 accountId) const
{
    std::scoped_lock lock(_mutex);
    auto it = _activeRootByAccount.find(accountId);
    if (it == _activeRootByAccount.end())
        return 0;
    return _groups.contains(it->second) ? it->second : 0;
}

bool GobGroupMgr::ComputeRelativeForMember(ObjectGuid::LowType rootGuid, ObjectGuid::LowType memberGuid,
    MemberRelativeTransform& out, std::string& error) const
{
    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(rootGuid);
    GameObjectData const* memberData = sObjectMgr->GetGameObjectData(memberGuid);
    if (!rootData || !memberData)
    {
        error = "Root or member spawn data missing";
        return false;
    }

    out = GobGroupTransform::ComputeRelative(SpawnPosition(*rootData), rootData->rotation,
        SpawnPosition(*memberData), memberData->rotation);
    if (!GobGroupTransform::IsFinite(Position(out.OffsetX, out.OffsetY, out.OffsetZ, out.OffsetO))
        || !GobGroupTransform::IsFinite(out.RelativeRotation))
    {
        error = "Computed relative transform is not finite";
        return false;
    }
    return true;
}

bool GobGroupMgr::PersistRelativeRows(ObjectGuid::LowType rootGuid,
    std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> const& rows, std::string& error)
{
    if (rows.empty())
        return true;

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    GobGroupTransform::AppendRelativeUpsertChunks(trans, rootGuid, rows);
    WorldDatabase.CommitTransaction(trans);
    return true;
}

bool GobGroupMgr::PersistSingleRelative(ObjectGuid::LowType rootGuid, ObjectGuid::LowType memberGuid,
    MemberRelativeTransform const& rel, std::string& error)
{
    std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> rows{ { memberGuid, rel } };
    return PersistRelativeRows(rootGuid, rows, error);
}

bool GobGroupMgr::Create(Player* player, ObjectGuid::LowType rootGuid, std::string const& name, std::string& error)
{
    if (!player)
    {
        error = "Player required";
        return false;
    }

    std::scoped_lock lock(_mutex);
    if (!ValidateSpawnForGroup(rootGuid, rootGuid, true, error))
        return false;

    std::string groupName = name;
    utf8truncate(groupName, 100);

    std::string escaped = groupName;
    WorldDatabase.EscapeString(escaped);

    WorldDatabase.PExecute(
        "INSERT INTO gameobject_group_root (root_guid, name, created_by) VALUES ({}, '{}', {})",
        rootGuid, escaped, player->GetSession()->GetAccountId());

    GroupRecord group;
    group.RootGuid = rootGuid;
    group.Name = groupName;
    group.CreatedBy = player->GetSession()->GetAccountId();
    _groups.emplace(rootGuid, std::move(group));
    _activeRootByAccount[player->GetSession()->GetAccountId()] = rootGuid;
    return true;
}

bool GobGroupMgr::AddMember(ObjectGuid::LowType groupGuid, ObjectGuid::LowType memberGuid, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    if (IsBusyUnlocked(rootGuid))
    {
        error = "Group is busy with a transform job";
        return false;
    }

    if (group->Members.size() >= GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Group {} already has max {} members", rootGuid, GOBGROUP_MAX_MEMBERS);
        return false;
    }

    if (!ValidateSpawnForGroup(memberGuid, rootGuid, false, error))
        return false;

    MemberRelativeTransform rel;
    if (!ComputeRelativeForMember(rootGuid, memberGuid, rel, error))
        return false;

    if (!PersistSingleRelative(rootGuid, memberGuid, rel, error))
        return false;

    group->Members.push_back(memberGuid);
    std::sort(group->Members.begin(), group->Members.end());
    group->Relatives[memberGuid] = rel;
    _memberToRoot[memberGuid] = rootGuid;
    return true;
}

bool GobGroupMgr::RemoveMember(ObjectGuid::LowType groupGuid, ObjectGuid::LowType memberGuid, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    if (IsBusyUnlocked(rootGuid))
    {
        error = "Group is busy with a transform job";
        return false;
    }

    if (memberGuid == rootGuid)
    {
        error = Trinity::StringFormat("GO {} is the group root; use dissolve to remove the group", memberGuid);
        return false;
    }

    auto it = std::find(group->Members.begin(), group->Members.end(), memberGuid);
    if (it == group->Members.end())
    {
        error = Trinity::StringFormat("GO {} is not a member of group {}", memberGuid, rootGuid);
        return false;
    }

    WorldDatabase.PExecute("DELETE FROM gameobject_group WHERE member_guid = {}", memberGuid);
    group->Members.erase(it);
    group->Relatives.erase(memberGuid);
    _memberToRoot.erase(memberGuid);
    return true;
}

bool GobGroupMgr::Dissolve(ObjectGuid::LowType groupGuid, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    if (IsBusyUnlocked(rootGuid))
    {
        error = "Group is busy with a transform job";
        return false;
    }

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    trans->PAppend("DELETE FROM gameobject_group WHERE root_guid = {}", rootGuid);
    trans->PAppend("DELETE FROM gameobject_group_root WHERE root_guid = {}", rootGuid);
    WorldDatabase.CommitTransaction(trans);

    UnindexGroup(*group);
    _jobs.erase(rootGuid);
    _groups.erase(rootGuid);

    for (auto it = _activeRootByAccount.begin(); it != _activeRootByAccount.end();)
    {
        if (it->second == rootGuid)
            it = _activeRootByAccount.erase(it);
        else
            ++it;
    }
    return true;
}

bool GobGroupMgr::DeleteFull(ObjectGuid::LowType groupGuid, bool fullForce, std::string& report)
{
    if (!fullForce)
    {
        report = "Full group deletion requires the exact full-force confirmation";
        return false;
    }

    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, report))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        report = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    if (IsBusyUnlocked(rootGuid))
    {
        report = "Group is busy with a transform job";
        return false;
    }

    // Delete members first and the root last. DeleteFromDB also despawns every
    // live instance and removes respawn/spawn-group/event/linked-respawn data.
    std::vector<ObjectGuid::LowType> spawnIds = group->Members;
    spawnIds.push_back(rootGuid);

    uint32 deleted = 0;
    std::vector<ObjectGuid::LowType> alreadyMissing;
    for (ObjectGuid::LowType spawnId : spawnIds)
    {
        if (GameObject::DeleteFromDB(spawnId))
            ++deleted;
        else
            alreadyMissing.push_back(spawnId);
    }

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    trans->PAppend("DELETE FROM gameobject_group WHERE root_guid = {}", rootGuid);
    trans->PAppend("DELETE FROM gameobject_group_root WHERE root_guid = {}", rootGuid);
    WorldDatabase.CommitTransaction(trans);

    UnindexGroup(*group);
    _jobs.erase(rootGuid);
    _groups.erase(rootGuid);

    for (auto it = _activeRootByAccount.begin(); it != _activeRootByAccount.end();)
    {
        if (it->second == rootGuid)
            it = _activeRootByAccount.erase(it);
        else
            ++it;
    }

    std::ostringstream oss;
    oss << "Group root " << rootGuid << " deleted full-force: "
        << deleted << '/' << spawnIds.size() << " GO removed from world and database";
    if (!alreadyMissing.empty())
    {
        oss << "; already missing:";
        for (ObjectGuid::LowType spawnId : alreadyMissing)
            oss << ' ' << spawnId;
    }
    report = oss.str();
    TC_LOG_INFO("scripts.noblenext.gobgroup", "{}", report);
    return true;
}

bool GobGroupMgr::CleanupOrphans(bool confirm, std::string& report)
{
    std::vector<ObjectGuid::LowType> orphanRoots;
    std::vector<ObjectGuid::LowType> orphanMembers;

    {
        std::scoped_lock lock(_mutex);
        if (QueryResult roots = WorldDatabase.Query("SELECT root_guid FROM gameobject_group_root"))
        {
            do
            {
                ObjectGuid::LowType rootGuid = roots->Fetch()[0].GetUInt64();
                if (!sObjectMgr->GetGameObjectData(rootGuid))
                    orphanRoots.push_back(rootGuid);
            } while (roots->NextRow());
        }

        if (QueryResult members = WorldDatabase.Query("SELECT member_guid, root_guid FROM gameobject_group"))
        {
            do
            {
                Field* fields = members->Fetch();
                ObjectGuid::LowType memberGuid = fields[0].GetUInt64();
                ObjectGuid::LowType rootGuid = fields[1].GetUInt64();
                bool orphan = !sObjectMgr->GetGameObjectData(memberGuid)
                    || !sObjectMgr->GetGameObjectData(rootGuid)
                    || !_groups.contains(rootGuid);
                if (orphan)
                    orphanMembers.push_back(memberGuid);
            } while (members->NextRow());
        }
    }

    std::ostringstream oss;
    oss << "Orphan roots: " << orphanRoots.size() << ", orphan members: " << orphanMembers.size();
    if (!confirm)
    {
        oss << " (dry-run; pass confirm to delete metadata only)";
        report = oss.str();
        return true;
    }

    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    for (ObjectGuid::LowType memberGuid : orphanMembers)
        trans->PAppend("DELETE FROM gameobject_group WHERE member_guid = {}", memberGuid);
    for (ObjectGuid::LowType rootGuid : orphanRoots)
    {
        trans->PAppend("DELETE FROM gameobject_group WHERE root_guid = {}", rootGuid);
        trans->PAppend("DELETE FROM gameobject_group_root WHERE root_guid = {}", rootGuid);
    }
    WorldDatabase.CommitTransaction(trans);

    LoadAndValidate();
    report = oss.str() + "; deleted";
    return true;
}

std::string GobGroupMgr::BuildInfo(ObjectGuid::LowType objectGuid) const
{
    std::scoped_lock lock(_mutex);

    GameObjectData const* objectData = sObjectMgr->GetGameObjectData(objectGuid);
    if (!objectData)
        return Trinity::StringFormat("GO {} not found in ObjectMgr", objectGuid);

    GameObjectTemplate const* objectTemplate = sObjectMgr->GetGameObjectTemplate(objectData->id);
    std::ostringstream oss;
    oss << "Object guid=" << objectGuid
        << " entry=" << objectData->id
        << " name='" << (objectTemplate ? objectTemplate->name : "<missing template>") << "'"
        << " map=" << objectData->mapId
        << " pos=(" << objectData->spawnPoint.GetPositionX() << ','
        << objectData->spawnPoint.GetPositionY() << ','
        << objectData->spawnPoint.GetPositionZ() << ','
        << objectData->spawnPoint.GetOrientation() << ')';

    ResolvedGroup resolved;
    if (!TryResolveGroupUnlocked(objectGuid, resolved))
    {
        oss << "\nGroup: none (this GO is neither root nor member).";
        return oss.str();
    }

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord const* group = FindGroup(rootGuid);
    if (!group)
    {
        oss << "\nGroup: cache miss for canonical root " << rootGuid;
        return oss.str();
    }

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(rootGuid);
    oss << "\nMembership: " << (resolved.IsRoot ? "root" : "member")
        << "; canonical root=" << rootGuid
        << "\nGroup root " << rootGuid
        << " name='" << group->Name << "'"
        << " members=" << group->Members.size()
        << " map=" << (rootData ? rootData->mapId : 0)
        << " dirty=" << (group->Dirty ? "yes" : "no")
        << " busy=" << (IsBusyUnlocked(rootGuid) ? "yes" : "no");
    if (group->Dirty && !group->DirtyReason.empty())
        oss << " (" << group->DirtyReason << ')';
    if (rootData)
    {
        GameObjectTemplate const* rootTemplate = sObjectMgr->GetGameObjectTemplate(rootData->id);
        oss << "\n  root guid=" << rootGuid
            << " entry=" << rootData->id
            << " name='" << (rootTemplate ? rootTemplate->name : "<missing template>") << "'"
            << " pos=(" << rootData->spawnPoint.GetPositionX() << ','
            << rootData->spawnPoint.GetPositionY() << ','
            << rootData->spawnPoint.GetPositionZ() << ','
            << rootData->spawnPoint.GetOrientation() << ')';
    }

    for (ObjectGuid::LowType member : group->Members)
    {
        GameObjectData const* memberData = sObjectMgr->GetGameObjectData(member);
        oss << "\n  member guid=" << member;
        if (!memberData)
        {
            oss << " <missing spawn>";
            continue;
        }

        GameObjectTemplate const* memberTemplate = sObjectMgr->GetGameObjectTemplate(memberData->id);
        oss << " entry=" << memberData->id
            << " name='" << (memberTemplate ? memberTemplate->name : "<missing template>") << "'"
            << " pos=(" << memberData->spawnPoint.GetPositionX() << ','
            << memberData->spawnPoint.GetPositionY() << ','
            << memberData->spawnPoint.GetPositionZ() << ','
            << memberData->spawnPoint.GetOrientation() << ')';

        if (auto relIt = group->Relatives.find(member); relIt != group->Relatives.end())
        {
            MemberRelativeTransform const& rel = relIt->second;
            oss << " offset=(" << rel.OffsetX << ',' << rel.OffsetY << ','
                << rel.OffsetZ << ',' << rel.OffsetO << ')';
        }
        else
            oss << " offset=<missing>";
    }
    return oss.str();
}

std::string GobGroupMgr::BuildCheck(ObjectGuid::LowType anyGuid) const
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    std::string error;
    if (!RequireGroupUnlocked(anyGuid, resolved, error))
        return error;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord const* group = FindGroup(rootGuid);
    if (!group)
        return "Group cache miss";

    if (!ValidateGroupIntegrity(*group, error))
        return Trinity::StringFormat("FAIL structural: {}", error);

    if (!CheckRelativeConsistency(*group, error))
        return Trinity::StringFormat("DIRTY drift: {} — run .gobject group recalc or sync", error);

    if (group->Dirty)
        return Trinity::StringFormat("DIRTY flagged: {}", group->DirtyReason.empty() ? "unknown" : group->DirtyReason);

    return Trinity::StringFormat("OK group {} ({} members)", rootGuid, group->Members.size());
}

char const* GobGroupMgr::PhaseName(JobPhase phase) const
{
    switch (phase)
    {
        case JobPhase::Queued: return "Queued";
        case JobPhase::Calculated: return "Calculated";
        case JobPhase::DbPending: return "DbPending";
        case JobPhase::RuntimePending: return "RuntimePending";
        case JobPhase::Completed: return "Completed";
        case JobPhase::Failed: return "Failed";
        default: return "None";
    }
}

std::string GobGroupMgr::BuildStatus(ObjectGuid::LowType anyGuid) const
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    std::string error;
    if (!RequireGroupUnlocked(anyGuid, resolved, error))
        return error;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord const* group = FindGroup(rootGuid);
    if (!group)
        return "Group cache miss";

    std::ostringstream oss;
    oss << "Group " << rootGuid << " members=" << group->Members.size()
        << " dirty=" << (group->Dirty ? "yes" : "no");

    auto jobIt = _jobs.find(rootGuid);
    if (jobIt == _jobs.end() || !jobIt->second)
    {
        oss << " job=none";
        if (!group->LastError.empty())
            oss << " lastError=" << group->LastError;
        return oss.str();
    }

    GroupJob const& job = *jobIt->second;
    oss << " job=" << PhaseName(job.Phase)
        << " go=" << job.Plan.Rows.size()
        << " sqlChunks=" << job.SqlChunks
        << " runtimeChunks=" << job.RuntimeChunks;
    if (!job.Error.empty())
        oss << " error=" << job.Error;
    return oss.str();
}

std::string GobGroupMgr::BuildList(Optional<uint32> mapId) const
{
    std::scoped_lock lock(_mutex);
    std::ostringstream oss;
    uint32 count = 0;
    for (auto const& [rootGuid, group] : _groups)
    {
        GameObjectData const* data = sObjectMgr->GetGameObjectData(rootGuid);
        if (mapId && (!data || data->mapId != *mapId))
            continue;
        oss << rootGuid << " '" << group.Name << "' members=" << group.Members.size()
            << " map=" << (data ? data->mapId : 0)
            << (group.Dirty ? " DIRTY" : "") << '\n';
        ++count;
    }
    if (!count)
        return "No groups";
    return oss.str();
}

namespace
{
    GobGroupObjectSnapshot MakeObjectSnapshot(ObjectGuid::LowType guid, bool inGroup, bool isRoot)
    {
        GobGroupObjectSnapshot snap;
        snap.Guid = guid;
        snap.InGroup = inGroup;
        snap.IsRoot = isRoot;
        if (GameObjectData const* data = sObjectMgr->GetGameObjectData(guid))
        {
            snap.Entry = data->id;
            snap.MapId = data->mapId;
            snap.X = data->spawnPoint.GetPositionX();
            snap.Y = data->spawnPoint.GetPositionY();
            snap.Z = data->spawnPoint.GetPositionZ();
            snap.O = data->spawnPoint.GetOrientation();
        }
        return snap;
    }
}

bool GobGroupMgr::TryGetInfoSnapshot(ObjectGuid::LowType objectGuid, GobGroupInfoSnapshot& out, std::string& error) const
{
    std::scoped_lock lock(_mutex);
    out = {};
    out.ObjectGuid = objectGuid;

    GameObjectData const* objectData = sObjectMgr->GetGameObjectData(objectGuid);
    if (!objectData)
    {
        error = Trinity::StringFormat("GO {} not found in ObjectMgr", objectGuid);
        return false;
    }

    ResolvedGroup resolved;
    bool const inGroup = TryResolveGroupUnlocked(objectGuid, resolved);
    out.Object = MakeObjectSnapshot(objectGuid, inGroup, inGroup && resolved.IsRoot);

    if (!inGroup)
        return true;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord const* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group cache miss for canonical root {}", rootGuid);
        return false;
    }

    out.GroupRoot = rootGuid;
    out.Name = group->Name;
    out.MemberCount = uint32(group->Members.size());
    out.Dirty = group->Dirty;
    out.Busy = IsBusyUnlocked(rootGuid);
    out.Members.reserve(group->Members.size());
    for (ObjectGuid::LowType member : group->Members)
        out.Members.push_back(MakeObjectSnapshot(member, true, false));
    return true;
}

void GobGroupMgr::GetListSnapshot(Optional<uint32> mapId, GobGroupListSnapshot& out) const
{
    std::scoped_lock lock(_mutex);
    out = {};
    out.MapFilter = mapId;
    out.Items.reserve(_groups.size());
    for (auto const& [rootGuid, group] : _groups)
    {
        GameObjectData const* data = sObjectMgr->GetGameObjectData(rootGuid);
        if (mapId && (!data || data->mapId != *mapId))
            continue;

        GobGroupListItemSnapshot item;
        item.RootGuid = rootGuid;
        item.Name = group.Name;
        item.MemberCount = uint32(group.Members.size());
        item.MapId = data ? data->mapId : 0;
        out.Items.push_back(std::move(item));
    }
}

void GobGroupMgr::GetNearSnapshot(Player const* player, float radius, GobGroupNearSnapshot& out) const
{
    out = {};
    if (!player)
        return;

    out.MapId = player->GetMapId();
    out.Radius = radius;
    if (!std::isfinite(radius) || radius <= 0.f)
        return;

    float const px = player->GetPositionX();
    float const py = player->GetPositionY();
    float const pz = player->GetPositionZ();
    float const radiusSq = radius * radius;

    std::scoped_lock lock(_mutex);
    out.Items.reserve(_groups.size());
    for (auto const& [rootGuid, group] : _groups)
    {
        // Use ObjectMgr spawn metadata rather than live Map objects. This intentionally
        // includes roots that are unloaded, inactive, or hidden by the player's phase.
        GameObjectData const* data = sObjectMgr->GetGameObjectData(rootGuid);
        if (!data || data->mapId != out.MapId)
            continue;

        float const dx = data->spawnPoint.GetPositionX() - px;
        float const dy = data->spawnPoint.GetPositionY() - py;
        float const dz = data->spawnPoint.GetPositionZ() - pz;
        float const distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq > radiusSq)
            continue;

        GobGroupNearItemSnapshot item;
        item.RootGuid = rootGuid;
        item.Name = group.Name;
        item.MemberCount = uint32(group.Members.size());
        item.MapId = data->mapId;
        item.Distance = std::sqrt(distanceSq);
        item.X = data->spawnPoint.GetPositionX();
        item.Y = data->spawnPoint.GetPositionY();
        item.Z = data->spawnPoint.GetPositionZ();
        out.Items.push_back(std::move(item));
    }

    std::sort(out.Items.begin(), out.Items.end(), [](GobGroupNearItemSnapshot const& left,
        GobGroupNearItemSnapshot const& right)
    {
        if (left.Distance != right.Distance)
            return left.Distance < right.Distance;
        return left.RootGuid < right.RootGuid;
    });
}

bool GobGroupMgr::TryGetStatusSnapshot(ObjectGuid::LowType anyGuid, GobGroupStatusSnapshot& out, std::string& error) const
{
    std::scoped_lock lock(_mutex);
    out = {};
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(anyGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord const* group = FindGroup(rootGuid);
    if (!group)
    {
        error = "Group cache miss";
        return false;
    }

    out.GroupGuid = rootGuid;
    out.Dirty = group->Dirty;
    out.Busy = IsBusyUnlocked(rootGuid);

    auto jobIt = _jobs.find(rootGuid);
    if (jobIt == _jobs.end() || !jobIt->second)
        out.JobState = "None";
    else
        out.JobState = PhaseName(jobIt->second->Phase);
    return true;
}

uint32 GobGroupMgr::ScanNear(Player* player, ObjectGuid::LowType groupGuid, float radius,
    std::vector<ObjectGuid::LowType>& outCandidates, std::string& report) const
{
    outCandidates.clear();
    if (!player)
    {
        report = "Player required";
        return 0;
    }

    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, report))
        return 0;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord const* group = FindGroup(rootGuid);
    if (!group)
    {
        report = Trinity::StringFormat("Group root {} not found", rootGuid);
        return 0;
    }

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(rootGuid);
    if (!rootData || rootData->mapId != player->GetMapId())
    {
        report = "Player must be on the same map as the group root";
        return 0;
    }

    GameObject* rootObject = nullptr;
    auto bounds = player->GetMap()->GetGameObjectBySpawnIdStore().equal_range(rootGuid);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        if (itr->second)
        {
            rootObject = itr->second;
            break;
        }
    }

    if (!rootObject)
    {
        report = "Group root must be loaded to scan nearby objects";
        return 0;
    }

    std::vector<GameObject*> nearby;
    rootObject->GetGameObjectListWithOptionsInGrid(nearby, radius, {});

    uint32 skipped = 0;
    std::ostringstream oss;
    for (GameObject* go : nearby)
    {
        if (!go || !go->GetSpawnId())
            continue;

        ObjectGuid::LowType spawnId = go->GetSpawnId();
        if (spawnId == rootGuid)
            continue;

        std::string error;
        if (!ValidateSpawnForGroup(spawnId, rootGuid, false, error))
        {
            ++skipped;
            continue;
        }

        // already in this group
        if (std::find(group->Members.begin(), group->Members.end(), spawnId) != group->Members.end())
        {
            ++skipped;
            continue;
        }

        outCandidates.push_back(spawnId);
    }

    std::sort(outCandidates.begin(), outCandidates.end());
    outCandidates.erase(std::unique(outCandidates.begin(), outCandidates.end()), outCandidates.end());

    oss << "Candidates: " << outCandidates.size() << ", skipped: " << skipped;
    if (!outCandidates.empty())
    {
        oss << " [";
        for (size_t i = 0; i < outCandidates.size(); ++i)
        {
            if (i)
                oss << ", ";
            oss << outCandidates[i];
            if (i >= 31)
            {
                oss << ", ...";
                break;
            }
        }
        oss << ']';
    }
    report = oss.str();
    return uint32(outCandidates.size());
}

bool GobGroupMgr::AddNear(Player* player, ObjectGuid::LowType groupGuid, float radius, bool confirm, std::string& report)
{
    std::vector<ObjectGuid::LowType> candidates;
    uint32 count = ScanNear(player, groupGuid, radius, candidates, report);
    if (!confirm)
    {
        report += " (dry-run; pass confirm to add)";
        return count > 0;
    }

    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, report))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        report = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    if (IsBusyUnlocked(rootGuid))
    {
        report = "Group is busy with a transform job";
        return false;
    }

    size_t const available = GOBGROUP_MAX_MEMBERS - group->Members.size();
    if (candidates.size() > available)
        candidates.resize(available);

    std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> rows;
    rows.reserve(candidates.size());
    uint32 failed = 0;
    for (ObjectGuid::LowType spawnId : candidates)
    {
        std::string error;
        if (!ValidateSpawnForGroup(spawnId, rootGuid, false, error))
        {
            ++failed;
            continue;
        }

        MemberRelativeTransform relative;
        if (!ComputeRelativeForMember(rootGuid, spawnId, relative, error))
        {
            ++failed;
            continue;
        }

        rows.emplace_back(spawnId, relative);
    }

    std::string error;
    if (!PersistRelativeRows(rootGuid, rows, error))
    {
        report = error.empty() ? "Failed to persist nearby members" : error;
        return false;
    }

    for (auto const& [memberGuid, relative] : rows)
    {
        group->Members.push_back(memberGuid);
        group->Relatives[memberGuid] = relative;
        _memberToRoot[memberGuid] = rootGuid;
    }
    std::sort(group->Members.begin(), group->Members.end());

    report = Trinity::StringFormat("Added {} in one DB transaction, failed {}. {}",
        rows.size(), failed, report);
    return !rows.empty();
}

bool GobGroupMgr::Capture(ObjectGuid::LowType anyGuid, bool silent, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(anyGuid, resolved, error))
    {
        if (silent)
            error.clear();
        return false;
    }

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    bool const isRoot = resolved.IsRoot;

    if (IsBusyUnlocked(rootGuid))
    {
        error = "Group is busy with a transform job";
        return false;
    }

    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = "Group cache miss";
        return false;
    }

    if (isRoot)
    {
        std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> rows;
        rows.reserve(group->Members.size());
        for (ObjectGuid::LowType member : group->Members)
        {
            MemberRelativeTransform rel;
            if (!ComputeRelativeForMember(rootGuid, member, rel, error))
            {
                MarkDirty(*group, error);
                return false;
            }
            rows.emplace_back(member, rel);
        }

        if (!PersistRelativeRows(rootGuid, rows, error))
        {
            MarkDirty(*group, error);
            return false;
        }

        for (auto const& [member, rel] : rows)
            group->Relatives[member] = rel;
        ClearDirty(*group);
        return true;
    }

    MemberRelativeTransform rel;
    if (!ComputeRelativeForMember(rootGuid, anyGuid, rel, error))
    {
        MarkDirty(*group, error);
        return false;
    }
    if (!PersistSingleRelative(rootGuid, anyGuid, rel, error))
    {
        MarkDirty(*group, error);
        return false;
    }
    group->Relatives[anyGuid] = rel;
    std::string drift;
    if (CheckRelativeConsistency(*group, drift))
        ClearDirty(*group);
    else
        MarkDirty(*group, drift);
    return true;
}

bool GobGroupMgr::Recalc(ObjectGuid::LowType rootGuid, std::string& error)
{
    return Capture(rootGuid, false, error);
}

bool GobGroupMgr::CollectSnapshot(GroupRecord const& group,
    std::unordered_map<ObjectGuid::LowType, Position>& positions,
    std::unordered_map<ObjectGuid::LowType, QuaternionData>& rotations,
    uint32& mapId, std::string& error) const
{
    positions.clear();
    rotations.clear();

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(group.RootGuid);
    if (!rootData)
    {
        error = "Root spawn missing";
        return false;
    }

    mapId = rootData->mapId;
    positions[group.RootGuid] = SpawnPosition(*rootData);
    rotations[group.RootGuid] = rootData->rotation;

    for (ObjectGuid::LowType member : group.Members)
    {
        GameObjectData const* data = sObjectMgr->GetGameObjectData(member);
        if (!data)
        {
            error = Trinity::StringFormat("Member {} spawn missing", member);
            return false;
        }
        if (data->mapId != mapId)
        {
            error = Trinity::StringFormat("Member {} map mismatch", member);
            return false;
        }
        positions[member] = SpawnPosition(*data);
        rotations[member] = data->rotation;
    }
    return true;
}

bool GobGroupMgr::EnqueueTransform(ObjectGuid::LowType rootGuid, Position const& newRootPos,
    QuaternionData const& newRootRotation, uint32 targetMapId, bool updateMap,
    bool requireRelativeConsistency, std::string& error)
{
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    if (requireRelativeConsistency)
    {
        if (group->Dirty)
        {
            error = Trinity::StringFormat("Group is dirty ({}); run recalc/sync first",
                group->DirtyReason.empty() ? "unknown" : group->DirtyReason);
            return false;
        }

        std::string drift;
        if (!CheckRelativeConsistency(*group, drift))
        {
            MarkDirty(*group, drift);
            error = Trinity::StringFormat("Group drift detected: {}; run recalc/sync first", drift);
            return false;
        }
    }

    auto jobIt = _jobs.find(rootGuid);
    if (jobIt != _jobs.end() && jobIt->second)
    {
        JobPhase const phase = jobIt->second->Phase;
        if (phase == JobPhase::DbPending || phase == JobPhase::RuntimePending)
        {
            error = "Group transform already in progress";
            return false;
        }
        // coalesce Queued/Calculated/terminal
    }

    std::unordered_map<ObjectGuid::LowType, Position> positions;
    std::unordered_map<ObjectGuid::LowType, QuaternionData> rotations;
    uint32 sourceMapId = 0;
    if (!CollectSnapshot(*group, positions, rotations, sourceMapId, error))
        return false;

    auto job = std::make_unique<GroupJob>();
    job->RootGuid = rootGuid;
    job->Phase = JobPhase::Queued;
    job->UpdateMap = updateMap;

    if (!GobGroupTransform::BuildPlan(rootGuid, sourceMapId, targetMapId, newRootPos, newRootRotation,
        group->Members, group->Relatives, positions, rotations, job->Plan, error))
    {
        return false;
    }

    job->Phase = JobPhase::Calculated;
    job->SqlChunks = uint32((job->Plan.Rows.size() + GOBGROUP_SQL_CHUNK_SIZE - 1) / GOBGROUP_SQL_CHUNK_SIZE);
    _jobs[rootGuid] = std::move(job);

    // Start from Update() so multiple commands received in the same world tick
    // can replace the calculated plan before the DB transaction is submitted.
    return true;
}

bool GobGroupMgr::StartJobDb(GroupJob& job)
{
    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    GobGroupTransform::AppendTransformUpdateChunks(trans, job.Plan, job.UpdateMap);
    job.Phase = JobPhase::DbPending;

    ObjectGuid::LowType rootGuid = job.RootGuid;
    TransactionCallback callback = WorldDatabase.AsyncCommitTransaction(trans);
    // AfterComplete runs from Update() while _mutex is already held — do not lock again.
    callback.AfterComplete([rootGuid](bool success)
    {
        GobGroupMgr& mgr = GobGroupMgr::Instance();
        auto it = mgr._jobs.find(rootGuid);
        if (it == mgr._jobs.end() || !it->second)
            return;

        GroupJob& active = *it->second;
        if (!success)
        {
            active.Phase = JobPhase::Failed;
            active.Error = "Async DB commit failed";
            if (GroupRecord* group = mgr.FindGroup(rootGuid))
                group->LastError = active.Error;
            return;
        }

        active.Phase = JobPhase::RuntimePending;
        active.Stage = RuntimeStage::Hide;
        active.RuntimeIndex = 0;
        mgr.ScheduleRuntime(active);
    });

    job.DbCallback = std::move(callback);
    return true;
}

void GobGroupMgr::ScheduleRuntime(GroupJob& job)
{
    // Prefer target map for reveal on cross-map; hide/apply on source first.
    uint32 scheduleMapId = job.Plan.SourceMapId;
    if (job.Stage == RuntimeStage::Reveal)
        scheduleMapId = job.Plan.TargetMapId;

    Map* map = sMapMgr->FindMap(scheduleMapId, 0);
    if (!map)
    {
        // Unloaded continent: apply cache/grid only from world thread path.
        // Still safe: no live GO to hide/show.
        if (job.Stage == RuntimeStage::Hide)
        {
            job.Stage = RuntimeStage::Apply;
            job.RuntimeIndex = 0;
        }

        if (job.Stage == RuntimeStage::Apply)
        {
            for (; job.RuntimeIndex < job.Plan.Rows.size(); ++job.RuntimeIndex)
            {
                GroupTransformRow const& row = job.Plan.Rows[job.RuntimeIndex];
                GameObjectData* data = const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(row.SpawnId));
                if (!data)
                {
                    job.Error = Trinity::StringFormat("Spawn {} disappeared during cache apply", row.SpawnId);
                    if (GroupRecord* group = FindGroup(job.RootGuid))
                        MarkDirty(*group, job.Error);
                    continue;
                }

                bool const gridChanged = row.OldMapId != row.NewMapId
                    || Trinity::ComputeGridCoord(row.OldWorld.GetPositionX(), row.OldWorld.GetPositionY())
                        != Trinity::ComputeGridCoord(row.NewWorld.GetPositionX(), row.NewWorld.GetPositionY());

                if (gridChanged)
                    sObjectMgr->RemoveGameobjectFromGrid(data);

                data->mapId = row.NewMapId;
                data->spawnPoint.Relocate(row.NewWorld);
                data->rotation = row.NewRotation;

                if (gridChanged)
                    sObjectMgr->AddGameobjectToGrid(data);
            }
            job.Stage = RuntimeStage::Reveal;
            job.RuntimeIndex = 0;
            ScheduleRuntime(job);
            return;
        }

        // No map to reveal on — treat as complete (will appear on grid load).
        job.Stage = RuntimeStage::Done;
        job.Phase = job.Error.empty() ? JobPhase::Completed : JobPhase::Failed;
        return;
    }

    ObjectGuid::LowType rootGuid = job.RootGuid;
    map->AddFarSpellCallback([rootGuid](Map* mapCtx)
    {
        GobGroupMgr::Instance().ProcessRuntime(mapCtx, rootGuid);
    });
}

void GobGroupMgr::ProcessRuntime(Map* map, ObjectGuid::LowType rootGuid)
{
    if (!map)
        return;

    std::scoped_lock lock(_mutex);
    auto it = _jobs.find(rootGuid);
    if (it == _jobs.end() || !it->second)
        return;

    GroupJob& job = *it->second;
    if (job.Phase != JobPhase::RuntimePending)
        return;

    uint32 const startMs = getMSTime();
    ++job.RuntimeChunks;

    auto findLive = [&](ObjectGuid::LowType spawnId) -> GameObject*
    {
        auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(spawnId);
        for (auto itr = bounds.first; itr != bounds.second; ++itr)
            if (itr->second)
                return itr->second;
        return nullptr;
    };

    if (job.Stage == RuntimeStage::Hide)
    {
        for (GroupTransformRow const& row : job.Plan.Rows)
        {
            if (GameObject* go = findLive(row.SpawnId))
            {
                go->DestroyForNearbyPlayers();
                go->Delete();
            }
        }
        job.Stage = RuntimeStage::Apply;
        job.RuntimeIndex = 0;
        ScheduleRuntime(job);
        return;
    }

    if (job.Stage == RuntimeStage::Apply)
    {
        uint32 processed = 0;
        while (job.RuntimeIndex < job.Plan.Rows.size()
            && processed < GOBGROUP_RUNTIME_CHUNK_SIZE
            && GetMSTimeDiffToNow(startMs) < GOBGROUP_RUNTIME_BUDGET_MS)
        {
            GroupTransformRow const& row = job.Plan.Rows[job.RuntimeIndex++];
            GameObjectData* data = const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(row.SpawnId));
            if (!data)
            {
                job.Error = Trinity::StringFormat("Spawn {} disappeared during runtime apply", row.SpawnId);
                if (GroupRecord* group = FindGroup(rootGuid))
                    MarkDirty(*group, job.Error);
                continue;
            }

            bool const gridChanged = row.OldMapId != row.NewMapId
                || Trinity::ComputeGridCoord(row.OldWorld.GetPositionX(), row.OldWorld.GetPositionY())
                    != Trinity::ComputeGridCoord(row.NewWorld.GetPositionX(), row.NewWorld.GetPositionY());

            if (gridChanged)
                sObjectMgr->RemoveGameobjectFromGrid(data);

            data->mapId = row.NewMapId;
            data->spawnPoint.Relocate(row.NewWorld);
            data->rotation = row.NewRotation;

            if (gridChanged)
                sObjectMgr->AddGameobjectToGrid(data);

            ++processed;
        }

        if (job.RuntimeIndex < job.Plan.Rows.size())
        {
            ScheduleRuntime(job);
            return;
        }

        job.Stage = RuntimeStage::Reveal;
        job.RuntimeIndex = 0;
        ScheduleRuntime(job);
        return;
    }

    if (job.Stage == RuntimeStage::Reveal)
    {
        // Final reveal of the whole group in one phase (budget measured separately).
        Map* revealMap = map;
        if (job.Plan.CrossMap)
        {
            revealMap = sMapMgr->FindMap(job.Plan.TargetMapId, 0);
            if (!revealMap)
            {
                job.Stage = RuntimeStage::Done;
                job.Phase = job.Error.empty() ? JobPhase::Completed : JobPhase::Failed;
                if (!job.Error.empty())
                {
                    if (GroupRecord* group = FindGroup(rootGuid))
                        group->LastError = job.Error;
                }
                return;
            }
            if (revealMap != map)
            {
                ObjectGuid::LowType rg = rootGuid;
                revealMap->AddFarSpellCallback([rg](Map* m)
                {
                    GobGroupMgr::Instance().ProcessRuntime(m, rg);
                });
                return;
            }
        }

        for (GroupTransformRow const& row : job.Plan.Rows)
        {
            if (!revealMap->IsGridLoaded(row.NewWorld))
                continue;

            if (!GameObject::CreateGameObjectFromDB(row.SpawnId, revealMap, true))
            {
                job.Error = Trinity::StringFormat("Failed to recreate spawn {} on map {}", row.SpawnId, row.NewMapId);
                if (GroupRecord* group = FindGroup(rootGuid))
                    MarkDirty(*group, job.Error);
            }
        }

        job.Stage = RuntimeStage::Done;
        job.Phase = job.Error.empty() ? JobPhase::Completed : JobPhase::Failed;
        if (GroupRecord* group = FindGroup(rootGuid))
        {
            if (job.Error.empty())
                group->LastError.clear();
            else
                group->LastError = job.Error;
        }
    }
}

void GobGroupMgr::CompleteJob(ObjectGuid::LowType rootGuid, bool success, std::string const& error)
{
    auto it = _jobs.find(rootGuid);
    if (it != _jobs.end() && it->second)
    {
        it->second->Phase = success ? JobPhase::Completed : JobPhase::Failed;
        it->second->Error = error;
    }
    if (GroupRecord* group = FindGroup(rootGuid))
    {
        if (!success)
        {
            group->LastError = error;
            MarkDirty(*group, error);
        }
    }
}

void GobGroupMgr::Update(uint32 /*diff*/)
{
    std::scoped_lock lock(_mutex);
    for (auto& [rootGuid, jobPtr] : _jobs)
    {
        if (!jobPtr || jobPtr->Phase != JobPhase::Calculated)
            continue;

        if (!StartJobDb(*jobPtr))
        {
            std::string const error = jobPtr->Error.empty() ? "Failed to start DB job" : jobPtr->Error;
            CompleteJob(rootGuid, false, error);
        }
    }

    for (auto& [rootGuid, jobPtr] : _jobs)
    {
        if (!jobPtr || !jobPtr->DbCallback)
            continue;
        if (jobPtr->Phase != JobPhase::DbPending)
            continue;

        // InvokeIfReady runs AfterComplete when future is ready.
        if (jobPtr->DbCallback->InvokeIfReady())
            jobPtr->DbCallback.reset();
    }

    // Terminal jobs remain available for `.gobject group status`
    // and are replaced atomically by the next calculated plan.
}

void GobGroupMgr::OnSingleObjectTransformSaved(ObjectGuid::LowType spawnId)
{
    std::string error;
    Capture(spawnId, true, error);
}

bool GobGroupMgr::CanDeleteGameObject(ObjectGuid::LowType spawnId, std::string& error) const
{
    std::scoped_lock lock(_mutex);

    ResolvedGroup resolved;
    if (!TryResolveGroupUnlocked(spawnId, resolved))
        return true;

    if (IsBusyUnlocked(resolved.RootGuid))
    {
        error = Trinity::StringFormat(
            "GO {} belongs to group {} with an active transform; wait for completion before deleting it",
            spawnId, resolved.RootGuid);
        return false;
    }

    return true;
}

void GobGroupMgr::OnGameObjectDeleted(ObjectGuid::LowType spawnId)
{
    std::scoped_lock lock(_mutex);

    if (GroupRecord* group = FindGroup(spawnId))
    {
        WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
        trans->PAppend("DELETE FROM gameobject_group WHERE root_guid = {}", spawnId);
        trans->PAppend("DELETE FROM gameobject_group_root WHERE root_guid = {}", spawnId);
        WorldDatabase.CommitTransaction(trans);

        UnindexGroup(*group);
        _jobs.erase(spawnId);
        _groups.erase(spawnId);

        for (auto it = _activeRootByAccount.begin(); it != _activeRootByAccount.end();)
        {
            if (it->second == spawnId)
                it = _activeRootByAccount.erase(it);
            else
                ++it;
        }
        return;
    }

    auto memberIt = _memberToRoot.find(spawnId);
    if (memberIt == _memberToRoot.end())
        return;

    ObjectGuid::LowType const rootGuid = memberIt->second;
    WorldDatabase.PExecute("DELETE FROM gameobject_group WHERE member_guid = {}", spawnId);

    if (GroupRecord* group = FindGroup(rootGuid))
    {
        std::erase(group->Members, spawnId);
        group->Relatives.erase(spawnId);
    }
    _memberToRoot.erase(memberIt);
}

bool GobGroupMgr::Sync(ObjectGuid::LowType groupGuid, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    std::unordered_map<ObjectGuid::LowType, Position> positions;
    std::unordered_map<ObjectGuid::LowType, QuaternionData> rotations;
    uint32 mapId = 0;
    if (!CollectSnapshot(*group, positions, rotations, mapId, error))
        return false;

    Position const rootPos = positions[rootGuid];
    QuaternionData const newRootRot = rotations[rootGuid];

    // Sync keeps root in place and rewrites members from saved relatives.
    bool const wasDirty = group->Dirty;
    std::string const previousDirtyReason = group->DirtyReason;
    ClearDirty(*group);
    if (!EnqueueTransform(rootGuid, rootPos, newRootRot, mapId, false, false, error))
    {
        group->Dirty = wasDirty;
        group->DirtyReason = previousDirtyReason;
        return false;
    }
    return true;
}

bool GobGroupMgr::Move(ObjectGuid::LowType groupGuid, Position const& newRootPos, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(rootGuid);
    if (!rootData)
    {
        error = "Root spawn missing";
        return false;
    }

    Position base = rootData->spawnPoint;
    QuaternionData baseRotation = rootData->rotation;
    if (auto jobIt = _jobs.find(rootGuid); jobIt != _jobs.end() && jobIt->second
        && jobIt->second->Phase == JobPhase::Calculated && !jobIt->second->Plan.Rows.empty())
    {
        base = jobIt->second->Plan.Rows.front().NewWorld;
        baseRotation = jobIt->second->Plan.Rows.front().NewRotation;
    }

    Position target = newRootPos;
    target.SetOrientation(base.GetOrientation());
    QuaternionData const newRootRot = GobGroupTransform::ApplyRootTilt(
        baseRotation, base.GetOrientation(), target.GetOrientation());

    return EnqueueTransform(rootGuid, target, newRootRot, rootData->mapId, false, true, error);
}

bool GobGroupMgr::Turn(ObjectGuid::LowType groupGuid, float newOrientation, std::string& error)
{
    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(rootGuid);
    if (!rootData)
    {
        error = "Root spawn missing";
        return false;
    }

    Position target = SpawnPosition(*rootData);
    QuaternionData baseRotation = rootData->rotation;
    if (auto jobIt = _jobs.find(rootGuid); jobIt != _jobs.end() && jobIt->second
        && jobIt->second->Phase == JobPhase::Calculated && !jobIt->second->Plan.Rows.empty())
    {
        target = jobIt->second->Plan.Rows.front().NewWorld;
        baseRotation = jobIt->second->Plan.Rows.front().NewRotation;
    }

    float const oldO = target.GetOrientation();
    target.SetOrientation(Position::NormalizeOrientation(newOrientation));
    QuaternionData const newRootRot = GobGroupTransform::ApplyRootTilt(baseRotation, oldO, target.GetOrientation());

    return EnqueueTransform(rootGuid, target, newRootRot, rootData->mapId, false, true, error);
}

bool GobGroupMgr::Relocate(ObjectGuid::LowType groupGuid, uint32 mapId, Position const& newRootPos,
    bool confirm, std::string& error)
{
    if (!confirm)
    {
        error = "Relocate requires confirm";
        return false;
    }

    std::scoped_lock lock(_mutex);
    ResolvedGroup resolved;
    if (!RequireGroupUnlocked(groupGuid, resolved, error))
        return false;

    ObjectGuid::LowType const rootGuid = resolved.RootGuid;
    GroupRecord* group = FindGroup(rootGuid);
    if (!group)
    {
        error = Trinity::StringFormat("Group root {} not found", rootGuid);
        return false;
    }

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(rootGuid);
    if (!rootData)
    {
        error = "Root spawn missing";
        return false;
    }

    MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
    if (!mapEntry)
    {
        error = Trinity::StringFormat("Invalid map {}", mapId);
        return false;
    }
    if (mapEntry->Instanceable())
    {
        error = "Instance/battleground/arena maps are not allowed";
        return false;
    }
    if (sObjectMgr->IsTransportMap(mapId))
    {
        error = "Transport maps are not allowed";
        return false;
    }
    if (!MapManager::IsValidMapCoord(mapId, newRootPos))
    {
        error = "Invalid target coordinates";
        return false;
    }

    QuaternionData const newRootRot = GobGroupTransform::ApplyRootTilt(
        rootData->rotation, rootData->spawnPoint.GetOrientation(), newRootPos.GetOrientation());

    return EnqueueTransform(rootGuid, newRootPos, newRootRot, mapId, true, true, error);
}

} // namespace RoleplayCore::NobleNext
