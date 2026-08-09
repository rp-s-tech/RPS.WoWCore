/*
 * NobleNext — GobBlueprint (AccountID templates + public/private + virtual center).
 *
 * Template-only mutations never touch world instances.
 * Spawn is DB-first: World → Roleplay → runtime publish; compensate World on RP failure.
 */

#include "noble_next_gobblueprint_mgr.h"
#include "../GobGroup/noble_next_gobgroup_protocol.h"

#include "Chat.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "RoleplayPhaseMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Util.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace RoleplayCore::NobleNext
{
namespace
{
    std::string EscapeSql(std::string value)
    {
        RoleplayDatabase.EscapeString(value);
        return value;
    }

    std::string DifficultyCsv(Difficulty difficulty)
    {
        return Trinity::StringFormat("{}", int32(difficulty));
    }

    void AppendGameObjectInsert(WorldDatabaseTransaction& trans, ObjectGuid::LowType spawnId, uint32 entry,
        uint32 mapId, Difficulty difficulty, Position const& pos, QuaternionData const& rot)
    {
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_INS_GAMEOBJECT);
        uint8 index = 0;
        stmt->setUInt64(index++, spawnId);
        stmt->setUInt32(index++, entry);
        stmt->setUInt16(index++, uint16(mapId));
        stmt->setString(index++, DifficultyCsv(difficulty));
        stmt->setUInt8(index++, 0);          // phaseUseFlags
        stmt->setUInt32(index++, 0);         // PhaseId (legacy)
        stmt->setUInt32(index++, 0);         // PhaseGroup
        stmt->setInt32(index++, -1);         // terrainSwapMap
        stmt->setFloat(index++, pos.GetPositionX());
        stmt->setFloat(index++, pos.GetPositionY());
        stmt->setFloat(index++, pos.GetPositionZ());
        stmt->setFloat(index++, pos.GetOrientation());
        stmt->setFloat(index++, rot.x);
        stmt->setFloat(index++, rot.y);
        stmt->setFloat(index++, rot.z);
        stmt->setFloat(index++, rot.w);
        stmt->setInt32(index++, 300);        // spawntimesecs
        stmt->setUInt8(index++, 255);        // animprogress
        stmt->setUInt8(index++, uint8(GO_STATE_READY));
        stmt->setString(index++, std::string()); // ScriptName
        stmt->setNull(index++);              // StringId
        stmt->setFloat(index++, -1.0f);      // size = template default
        stmt->setFloat(index++, 0.0f);       // visibility
        trans->Append(stmt);
    }

    void FillStagedGameObjectData(ObjectGuid::LowType spawnId, uint32 entry, uint32 mapId,
        Difficulty difficulty, Position const& pos, QuaternionData const& rot)
    {
        GameObjectData& data = sObjectMgr->NewOrExistGameObjectData(spawnId);
        data.spawnId = spawnId;
        data.id = entry;
        data.mapId = mapId;
        data.spawnPoint.Relocate(pos);
        data.rotation = rot;
        data.spawntimesecs = 300;
        data.animprogress = 255;
        data.goState = GO_STATE_READY;
        data.spawnDifficulties = { difficulty };
        data.artKit = 0;
        data.size = -1.0f;
        if (!data.spawnGroupData)
            data.spawnGroupData = sObjectMgr->GetDefaultSpawnGroup();
    }

    char const* PartTypeToSql(GobBlueprintPartType type)
    {
        switch (type)
        {
            case GobBlueprintPartType::Object: return "object";
            case GobBlueprintPartType::Group:  return "group";
            case GobBlueprintPartType::Base:
            default:                           return "base";
        }
    }

    GobBlueprintPartType PartTypeFromSql(std::string const& value)
    {
        if (value == "object")
            return GobBlueprintPartType::Object;
        if (value == "group")
            return GobBlueprintPartType::Group;
        return GobBlueprintPartType::Base;
    }

    QuaternionData IdentityQuat()
    {
        return QuaternionData(0.f, 0.f, 0.f, 1.f);
    }

    MemberRelativeTransform IdentityRelative()
    {
        MemberRelativeTransform rel;
        rel.RelativeRotation = IdentityQuat();
        return rel;
    }

    bool IsAllDigits(std::string const& text)
    {
        if (text.empty())
            return false;
        return std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
    }

    bool ValidateEntry(uint32 entry, std::string& error)
    {
        if (!entry)
        {
            error = "entry=0 допустим только для виртуального центра";
            return false;
        }
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(entry);
        if (!info)
        {
            error = Trinity::StringFormat("Нет gameobject_template для entry {}", entry);
            return false;
        }
        if (info->displayId && !sGameObjectDisplayInfoStore.LookupEntry(info->displayId))
        {
            error = Trinity::StringFormat("У entry {} некорректный displayId", entry);
            return false;
        }
        return true;
    }

    Position MemberWorldPos(Position const& centerPos, QuaternionData const& centerRot,
        MemberRelativeTransform const& rel)
    {
        Position world = GobGroupTransform::ApplyLocalOffset(centerPos, centerRot, rel);
        world.SetOrientation(Position::NormalizeOrientation(rel.OffsetO + centerPos.GetOrientation()));
        return world;
    }

    QuaternionData MemberWorldRot(QuaternionData const& centerRot, MemberRelativeTransform const& rel)
    {
        return GobGroupTransform::ApplyRelativeRotation(rel.RelativeRotation, centerRot);
    }

    struct SynthPose
    {
        Position Pos;
        QuaternionData Rot;
        uint32 Entry = 0;
        uint32 MemberId = 0;
        uint32 PartId = 0;
        bool IsRoot = false;
    };

    std::vector<SynthPose> SynthesizeWorldPoses(std::vector<GobBlueprintMember> const& members)
    {
        std::vector<SynthPose> poses;
        poses.reserve(members.size());
        if (members.empty())
            return poses;

        GobBlueprintMember const* root = nullptr;
        for (GobBlueprintMember const& m : members)
        {
            if (m.IsRoot)
            {
                root = &m;
                break;
            }
        }
        if (!root)
            root = &members.front();

        QuaternionData const centerRot = GobGroupTransform::ApplyRelativeRotation(
            root->Relative.RelativeRotation, GobGroupTransform::YawQuat(0.f));
        Position const centerPos(0.f, 0.f, 0.f, 0.f);

        for (GobBlueprintMember const& m : members)
        {
            SynthPose pose;
            pose.Entry = m.Entry;
            pose.MemberId = m.Id;
            pose.PartId = m.PartId;
            pose.IsRoot = m.IsRoot;
            if (m.IsRoot)
            {
                pose.Pos = centerPos;
                pose.Rot = centerRot;
            }
            else
            {
                pose.Pos = MemberWorldPos(centerPos, centerRot, m.Relative);
                pose.Rot = MemberWorldRot(centerRot, m.Relative);
            }
            poses.push_back(pose);
        }
        return poses;
    }

    void AppendMemberInsertChunks(RoleplayDatabaseTransaction& trans, uint32 blueprintId, uint32 partId,
        std::vector<GobBlueprintMember> const& members)
    {
        for (size_t offset = 0; offset < members.size(); offset += GOBGROUP_SQL_CHUNK_SIZE)
        {
            size_t const end = std::min(offset + GOBGROUP_SQL_CHUNK_SIZE, members.size());
            std::ostringstream oss;
            oss << "INSERT INTO gameobject_blueprint_member "
                "(blueprint_id, part_id, sort_order, is_root, entry, offset_x, offset_y, offset_z, offset_o, "
                "rotation0, rotation1, rotation2, rotation3) VALUES ";
            for (size_t i = offset; i < end; ++i)
            {
                GobBlueprintMember const& m = members[i];
                if (i != offset)
                    oss << ',';
                oss << '(' << blueprintId << ',' << partId << ',' << m.SortOrder << ','
                    << (m.IsRoot ? 1 : 0) << ',' << m.Entry << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.OffsetX) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.OffsetY) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.OffsetZ) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.OffsetO) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.x) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.y) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.z) << ','
                    << GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.w) << ')';
            }
            trans->Append(oss.str().c_str());
        }
    }

    std::vector<GobBlueprintMember> SnapshotToMembers(GobGroupRelativeSnapshot const& snap, QuaternionData const& rootTilt)
    {
        std::vector<GobBlueprintMember> members;
        members.reserve(snap.Members.size());
        for (size_t i = 0; i < snap.Members.size(); ++i)
        {
            GobBlueprintMember row;
            row.SortOrder = uint32(i);
            row.IsRoot = (i == 0);
            row.Entry = snap.Members[i].Entry;
            row.Relative = snap.Members[i].Relative;
            if (i == 0)
            {
                row.Relative.OffsetX = row.Relative.OffsetY = row.Relative.OffsetZ = 0.f;
                row.Relative.OffsetO = 0.f;
                row.Relative.RelativeRotation = rootTilt;
            }
            members.push_back(row);
        }
        return members;
    }

    bool CleanupEmptyPart(uint32 partId, uint32 blueprintId)
    {
        QueryResult still = RoleplayDatabase.PQuery(
            "SELECT COUNT(*) FROM gameobject_blueprint_member WHERE part_id = {} AND blueprint_id = {}",
            partId, blueprintId);
        uint32 count = 0;
        if (still)
            if (Field* f = still->Fetch())
                count = f[0].GetUInt32();
        if (count == 0)
        {
            RoleplayDatabase.DirectPExecute(
                "DELETE FROM gameobject_blueprint_part WHERE id = {} AND blueprint_id = {} AND part_type <> 'base'",
                partId, blueprintId);
            return true;
        }
        return false;
    }
}

GobBlueprintMgr& GobBlueprintMgr::Instance()
{
    static GobBlueprintMgr instance;
    return instance;
}

uint32 GobBlueprintMgr::GetAccountId(Player* player) const
{
    return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
}

uint32 GobBlueprintMgr::GetBnetAccountId(Player* player) const
{
    return player && player->GetSession() ? player->GetSession()->GetBattlenetAccountId() : 0;
}

bool GobBlueprintMgr::HasStaffOverride(Player* player) const
{
    return player && player->GetSession()
        && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_STAFF);
}

bool GobBlueprintMgr::NormalizeName(std::string& name, std::string& error) const
{
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
        name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
        name.pop_back();
    utf8truncate(name, 100);
    if (name.empty())
    {
        error = "Нужно указать имя шаблона";
        return false;
    }
    return true;
}

bool GobBlueprintMgr::ParseKey(std::string const& text, uint32& ownerAccountId, uint32& blueprintId) const
{
    size_t const dash = text.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= text.size())
        return false;
    if (text.find('-', dash + 1) != std::string::npos)
        return false;

    std::string const left = text.substr(0, dash);
    std::string const right = text.substr(dash + 1);
    if (!IsAllDigits(left) || !IsAllDigits(right))
        return false;

    try
    {
        unsigned long long const owner = std::stoull(left);
        unsigned long long const id = std::stoull(right);
        if (!owner || !id || owner > UINT32_MAX || id > UINT32_MAX)
            return false;
        ownerAccountId = uint32(owner);
        blueprintId = uint32(id);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::string GobBlueprintMgr::FormatKey(uint32 ownerAccountId, uint32 blueprintId) const
{
    return Trinity::StringFormat("{}-{}", ownerAccountId, blueprintId);
}

bool GobBlueprintMgr::CanDiscover(Player* player, GobBlueprintRecord const& bp) const
{
    if (!player)
        return false;
    uint32 const accountId = GetAccountId(player);
    if (bp.OwnerAccountId == accountId)
        return true;
    if (bp.IsPublic)
        return true;
    return HasStaffOverride(player);
}

bool GobBlueprintMgr::CanSpawn(Player* player, GobBlueprintRecord const& bp) const
{
    return CanDiscover(player, bp);
}

bool GobBlueprintMgr::CanMutate(Player* player, GobBlueprintRecord const& bp) const
{
    if (!player)
        return false;
    if (bp.OwnerAccountId == GetAccountId(player))
        return true;
    return HasStaffOverride(player);
}

bool GobBlueprintMgr::CanSetPublic(Player* player, GobBlueprintRecord const& bp) const
{
    if (!CanMutate(player, bp))
        return false;
    return player->GetSession()
        && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GOBJECT_BLUEPRINT_SET_PUBLIC);
}

void GobBlueprintMgr::FillCapabilities(Player* player, GobBlueprintRecord& bp) const
{
    bp.CanMutate = CanMutate(player, bp);
    bp.CanSetPublic = CanSetPublic(player, bp);
}

void GobBlueprintMgr::FillCapabilities(Player* player, GobBlueprintListItem& item) const
{
    GobBlueprintRecord tmp;
    tmp.Id = item.Id;
    tmp.OwnerAccountId = item.OwnerAccountId;
    tmp.IsPublic = item.IsPublic;
    item.CanMutate = CanMutate(player, tmp);
}

bool GobBlueprintMgr::LoadMembersAndParts(GobBlueprintRecord& out, std::string& error) const
{
    out.Parts.clear();
    out.Members.clear();

    if (QueryResult parts = RoleplayDatabase.PQuery(
        "SELECT id, part_type, label, source_root_guid FROM gameobject_blueprint_part "
        "WHERE blueprint_id = {} ORDER BY id ASC", out.Id))
    {
        do
        {
            Field* f = parts->Fetch();
            GobBlueprintPart part;
            part.Id = f[0].GetUInt32();
            part.Type = PartTypeFromSql(f[1].GetString());
            part.Label = f[2].GetString();
            part.SourceRootGuid = f[3].GetUInt64();
            out.Parts.push_back(std::move(part));
        } while (parts->NextRow());
    }

    if (QueryResult members = RoleplayDatabase.PQuery(
        "SELECT id, part_id, sort_order, is_root, entry, offset_x, offset_y, offset_z, offset_o, "
        "rotation0, rotation1, rotation2, rotation3 "
        "FROM gameobject_blueprint_member WHERE blueprint_id = {} "
        "ORDER BY is_root DESC, sort_order ASC, id ASC", out.Id))
    {
        do
        {
            Field* m = members->Fetch();
            GobBlueprintMember row;
            row.Id = m[0].GetUInt32();
            row.PartId = m[1].GetUInt32();
            row.SortOrder = m[2].GetUInt32();
            row.IsRoot = m[3].GetUInt8() != 0;
            row.Entry = m[4].GetUInt32();
            row.Relative.OffsetX = m[5].GetFloat();
            row.Relative.OffsetY = m[6].GetFloat();
            row.Relative.OffsetZ = m[7].GetFloat();
            row.Relative.OffsetO = m[8].GetFloat();
            row.Relative.RelativeRotation = QuaternionData(m[9].GetFloat(), m[10].GetFloat(), m[11].GetFloat(), m[12].GetFloat());
            out.Members.push_back(row);
        } while (members->NextRow());
    }

    uint32 rootCount = 0;
    for (GobBlueprintMember const& m : out.Members)
        if (m.IsRoot)
            ++rootCount;
    if (!out.Members.empty() && rootCount != 1)
    {
        error = Trinity::StringFormat("Шаблон {} повреждён: ожидался ровно один root, найдено {}", out.Id, rootCount);
        return false;
    }
    return true;
}

bool GobBlueprintMgr::LoadBlueprintById(uint32 blueprintId, GobBlueprintRecord& out, std::string& error) const
{
    out = {};
    QueryResult result = RoleplayDatabase.PQuery(
        "SELECT id, owner_account_id, name, description, is_public, deleted "
        "FROM gameobject_blueprint WHERE id = {} LIMIT 1", blueprintId);
    if (!result)
    {
        error = Trinity::StringFormat("Шаблон id={} не найден", blueprintId);
        return false;
    }

    Field* fields = result->Fetch();
    out.Id = fields[0].GetUInt32();
    out.OwnerAccountId = fields[1].GetUInt32();
    out.Name = fields[2].GetString();
    out.Description = fields[3].GetString();
    out.IsPublic = fields[4].GetUInt8() != 0;
    out.Deleted = fields[5].GetUInt8() != 0;
    if (out.Deleted)
    {
        error = Trinity::StringFormat("Шаблон «{}» удалён", out.Name);
        return false;
    }
    return LoadMembersAndParts(out, error);
}

bool GobBlueprintMgr::ResolveBlueprint(Player* player, std::string const& keyOrName, bool forMutate, bool requirePhysical,
    GobBlueprintRecord& out, std::string& error) const
{
    out = {};
    if (!player)
    {
        error = "Нужен игрок";
        return false;
    }

    uint32 keyOwner = 0;
    uint32 keyId = 0;
    if (ParseKey(keyOrName, keyOwner, keyId))
    {
        if (!LoadBlueprintById(keyId, out, error))
            return false;
        if (out.OwnerAccountId != keyOwner)
        {
            error = Trinity::StringFormat("Ключ «{}» не совпадает с владельцем шаблона", keyOrName);
            return false;
        }
    }
    else
    {
        std::string name = keyOrName;
        if (!NormalizeName(name, error))
            return false;

        uint32 const accountId = GetAccountId(player);
        std::string const escaped = EscapeSql(name);
        QueryResult result = RoleplayDatabase.PQuery(
            "SELECT id FROM gameobject_blueprint WHERE owner_account_id = {} AND name = '{}' AND deleted = 0 LIMIT 1",
            accountId, escaped);
        if (!result)
        {
            error = Trinity::StringFormat("Шаблон «{}» не найден среди ваших", name);
            return false;
        }
        if (!LoadBlueprintById(result->Fetch()[0].GetUInt32(), out, error))
            return false;
    }

    if (!CanDiscover(player, out))
    {
        error = "Нет доступа к этому шаблону";
        return false;
    }
    if (forMutate && !CanMutate(player, out))
    {
        error = "Изменять шаблон может только владелец или staff";
        return false;
    }
    if (requirePhysical && out.PhysicalMemberCount() < 1)
    {
        error = Trinity::StringFormat(
            "Шаблон «{}» без физических GO (только виртуальный центр). Добавьте объекты или перезапишите из группы.",
            out.Name);
        return false;
    }

    FillCapabilities(player, out);

    if (forMutate && HasStaffOverride(player) && out.OwnerAccountId != GetAccountId(player))
    {
        sRoleplayPhaseMgr.WriteAudit(player->GetGUID().GetCounter(), GetAccountId(player),
            "gobject_blueprint_staff_mutate", 0,
            Trinity::StringFormat(R"({{"key":"{}","name":"{}"}})", out.Key(), out.Name));
    }
    return true;
}

bool GobBlueprintMgr::CaptureActiveGroup(Player* player, GobGroupRelativeSnapshot& snap, QuaternionData& rootTilt,
    std::string& error) const
{
    snap = {};
    rootTilt = IdentityQuat();

    if (!player || !player->GetSession())
    {
        error = "Нужен игрок";
        return false;
    }

    ObjectGuid::LowType const active = sGobGroupMgr.GetActiveRoot(player->GetSession()->GetAccountId());
    if (!active)
    {
        error = "Нет активной группы — сначала .gobject group use <guid> или вкладка GobGroup";
        return false;
    }
    if (sGobGroupMgr.IsBusy(active))
    {
        error = "У активной группы идёт transform — дождитесь завершения";
        return false;
    }
    if (!sGobGroupMgr.TryGetRelativeSnapshot(active, snap, error))
        return false;

    GameObjectData const* rootData = sObjectMgr->GetGameObjectData(snap.RootGuid);
    if (!rootData)
    {
        error = Trinity::StringFormat("У root {} нет GameObjectData", snap.RootGuid);
        return false;
    }

    float const rootO = rootData->spawnPoint.GetOrientation();
    rootTilt = GobGroupTransform::ComputeRelativeRotation(rootData->rotation, GobGroupTransform::YawQuat(rootO));
    if (!GobGroupTransform::IsFinite(rootTilt))
    {
        error = "Некорректный tilt корня активной группы";
        return false;
    }

    if (!snap.Members.empty())
    {
        snap.Members[0].Relative.OffsetX = 0.f;
        snap.Members[0].Relative.OffsetY = 0.f;
        snap.Members[0].Relative.OffsetZ = 0.f;
        snap.Members[0].Relative.OffsetO = 0.f;
        // Keep root tilt in RelativeRotation (do NOT force identity).
        snap.Members[0].Relative.RelativeRotation = rootTilt;
    }
    return true;
}

bool GobBlueprintMgr::PersistMembers(RoleplayDatabaseTransaction& trans, uint32 blueprintId, uint32 partId,
    std::vector<GobBlueprintMember> const& members)
{
    if (members.empty())
        return true;
    AppendMemberInsertChunks(trans, blueprintId, partId, members);
    return true;
}

bool GobBlueprintMgr::ReplaceComposition(uint32 blueprintId, std::vector<GobBlueprintMember> const& members,
    ObjectGuid::LowType sourceRootGuid, std::string& error)
{
    if (members.empty() || members.size() > 1 + GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Число members в шаблоне должно быть 1..{}", 1 + GOBGROUP_MAX_MEMBERS);
        return false;
    }

    uint32 physical = 0;
    uint32 roots = 0;
    for (GobBlueprintMember const& m : members)
    {
        if (m.IsRoot)
            ++roots;
        if (m.Entry != 0)
            ++physical;
    }
    if (roots != 1)
    {
        error = "В составе шаблона должен быть ровно один root";
        return false;
    }
    if (physical > GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Слишком много физических GO (лимит {})", GOBGROUP_MAX_MEMBERS);
        return false;
    }

    RoleplayDatabaseTransaction trans = RoleplayDatabase.BeginTransaction();
    trans->PAppend("DELETE FROM gameobject_blueprint_member WHERE blueprint_id = {}", blueprintId);
    trans->PAppend("DELETE FROM gameobject_blueprint_part WHERE blueprint_id = {}", blueprintId);
    trans->PAppend(
        "INSERT INTO gameobject_blueprint_part (blueprint_id, part_type, label, source_root_guid) "
        "VALUES ({}, 'base', '', {})",
        blueprintId, uint64(sourceRootGuid));
    RoleplayDatabase.DirectCommitTransaction(trans);

    QueryResult partResult = RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint_part WHERE blueprint_id = {} AND part_type = 'base' "
        "ORDER BY id DESC LIMIT 1", blueprintId);
    if (!partResult)
    {
        error = "Не удалось создать base-part шаблона";
        return false;
    }
    uint32 const partId = partResult->Fetch()[0].GetUInt32();

    RoleplayDatabaseTransaction memberTrans = RoleplayDatabase.BeginTransaction();
    if (!PersistMembers(memberTrans, blueprintId, partId, members))
    {
        error = "Не удалось подготовить INSERT members";
        return false;
    }
    RoleplayDatabase.DirectCommitTransaction(memberTrans);

    QueryResult countResult = RoleplayDatabase.PQuery(
        "SELECT COUNT(*) FROM gameobject_blueprint_member WHERE blueprint_id = {}", blueprintId);
    uint32 actual = 0;
    if (countResult)
        if (Field* f = countResult->Fetch())
            actual = f[0].GetUInt32();
    if (actual != members.size())
    {
        error = Trinity::StringFormat(
            "Не удалось записать members шаблона id={} (ожидали {}, получили {})",
            blueprintId, members.size(), actual);
        return false;
    }
    return true;
}

bool GobBlueprintMgr::HasBusySpawn(uint32 accountId) const
{
    auto it = _spawnJobs.find(accountId);
    if (it == _spawnJobs.end() || !it->second)
        return false;
    SpawnPhase const phase = it->second->Phase;
    return phase == SpawnPhase::WorldPending
        || phase == SpawnPhase::RoleplayPending
        || phase == SpawnPhase::RuntimePending;
}

bool GobBlueprintMgr::List(Player* player, GobBlueprintListScope scope, std::string const& filter,
    std::vector<GobBlueprintListItem>& out, std::string& error)
{
    out.clear();
    if (!player)
    {
        error = "Нужен игрок";
        return false;
    }

    uint32 const accountId = GetAccountId(player);
    bool const staff = HasStaffOverride(player);
    std::string like = filter;
    utf8truncate(like, 100);
    std::string const escaped = EscapeSql(like);

    std::string where;
    switch (scope)
    {
        case GobBlueprintListScope::Mine:
            where = Trinity::StringFormat("b.owner_account_id = {} AND b.deleted = 0", accountId);
            break;
        case GobBlueprintListScope::Public:
            where = "b.is_public = 1 AND b.deleted = 0";
            break;
        case GobBlueprintListScope::All:
            if (staff)
                where = "b.deleted = 0";
            else
                where = Trinity::StringFormat(
                    "b.deleted = 0 AND (b.owner_account_id = {} OR b.is_public = 1)", accountId);
            break;
    }

    if (!like.empty())
        where += Trinity::StringFormat(" AND b.name LIKE '%{}%'", escaped);

    QueryResult result = RoleplayDatabase.PQuery(
        "SELECT b.id, b.owner_account_id, b.name, b.description, b.is_public, "
        "COALESCE(SUM(CASE WHEN m.entry <> 0 THEN 1 ELSE 0 END), 0) "
        "FROM gameobject_blueprint b "
        "LEFT JOIN gameobject_blueprint_member m ON m.blueprint_id = b.id "
        "WHERE {} "
        "GROUP BY b.id, b.owner_account_id, b.name, b.description, b.is_public "
        "ORDER BY b.name ASC",
        where);

    if (!result)
        return true;

    do
    {
        Field* f = result->Fetch();
        GobBlueprintListItem item;
        item.Id = f[0].GetUInt32();
        item.OwnerAccountId = f[1].GetUInt32();
        item.Name = f[2].GetString();
        item.Description = f[3].GetString();
        item.IsPublic = f[4].GetUInt8() != 0;
        item.MemberCount = f[5].GetUInt32();

        GobBlueprintRecord acl;
        acl.Id = item.Id;
        acl.OwnerAccountId = item.OwnerAccountId;
        acl.IsPublic = item.IsPublic;
        if (!CanDiscover(player, acl))
            continue;

        FillCapabilities(player, item);
        out.push_back(std::move(item));
    } while (result->NextRow());
    return true;
}

bool GobBlueprintMgr::Info(Player* player, std::string const& keyOrName, GobBlueprintRecord& out, std::string& error)
{
    return ResolveBlueprint(player, keyOrName, false, false, out, error);
}

bool GobBlueprintMgr::NewFromActiveGroup(Player* player, std::string const& name, std::string& error)
{
    if (!player)
    {
        error = "Нужен игрок";
        return false;
    }

    std::string normalized = name;
    if (!NormalizeName(normalized, error))
        return false;

    GobGroupRelativeSnapshot snap;
    QuaternionData rootTilt;
    if (!CaptureActiveGroup(player, snap, rootTilt, error))
        return false;
    if (snap.Members.empty())
    {
        error = "Активная группа пуста — нечего сохранять в шаблон";
        return false;
    }
    if (snap.Members.size() > GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Активная группа превышает лимит шаблона ({})", GOBGROUP_MAX_MEMBERS);
        return false;
    }

    uint32 const owner = GetAccountId(player);
    std::string const escaped = EscapeSql(normalized);
    if (RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint WHERE owner_account_id = {} AND name = '{}' AND deleted = 0 LIMIT 1",
        owner, escaped))
    {
        error = Trinity::StringFormat("Шаблон «{}» уже существует", normalized);
        return false;
    }

    std::vector<GobBlueprintMember> const members = SnapshotToMembers(snap, rootTilt);

    // Step 1: header → known blueprint id
    RoleplayDatabase.DirectPExecute(
        "INSERT INTO gameobject_blueprint (owner_account_id, name, description, is_public, deleted) "
        "VALUES ({}, '{}', '', 0, 0)",
        owner, escaped);

    QueryResult idResult = RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint WHERE owner_account_id = {} AND name = '{}' AND deleted = 0 LIMIT 1",
        owner, escaped);
    if (!idResult)
    {
        error = "Не удалось записать шаблон в БД";
        return false;
    }
    uint32 const blueprintId = idResult->Fetch()[0].GetUInt32();

    // Step 2: base part → known part id
    RoleplayDatabase.DirectPExecute(
        "INSERT INTO gameobject_blueprint_part (blueprint_id, part_type, label, source_root_guid) "
        "VALUES ({}, 'base', '', {})",
        blueprintId, uint64(snap.RootGuid));

    QueryResult partResult = RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint_part WHERE blueprint_id = {} ORDER BY id DESC LIMIT 1",
        blueprintId);
    if (!partResult)
    {
        RoleplayDatabase.DirectPExecute("DELETE FROM gameobject_blueprint WHERE id = {}", blueprintId);
        error = "Не удалось создать base-part шаблона";
        return false;
    }
    uint32 const partId = partResult->Fetch()[0].GetUInt32();

    // Step 3: members with known ids
    RoleplayDatabaseTransaction trans = RoleplayDatabase.BeginTransaction();
    PersistMembers(trans, blueprintId, partId, members);
    RoleplayDatabase.DirectCommitTransaction(trans);

    QueryResult countResult = RoleplayDatabase.PQuery(
        "SELECT COUNT(*), COALESCE(SUM(CASE WHEN entry <> 0 THEN 1 ELSE 0 END), 0) "
        "FROM gameobject_blueprint_member WHERE blueprint_id = {}", blueprintId);
    uint32 actualTotal = 0;
    uint32 actualPhysical = 0;
    if (countResult)
    {
        Field* f = countResult->Fetch();
        actualTotal = f[0].GetUInt32();
        actualPhysical = f[1].GetUInt32();
    }

    uint32 expectedPhysical = 0;
    for (GobBlueprintMember const& m : members)
        if (m.Entry != 0)
            ++expectedPhysical;

    if (actualTotal != members.size() || actualPhysical != expectedPhysical)
    {
        RoleplayDatabase.DirectPExecute("DELETE FROM gameobject_blueprint_member WHERE blueprint_id = {}", blueprintId);
        RoleplayDatabase.DirectPExecute("DELETE FROM gameobject_blueprint_part WHERE blueprint_id = {}", blueprintId);
        RoleplayDatabase.DirectPExecute("DELETE FROM gameobject_blueprint WHERE id = {}", blueprintId);
        error = Trinity::StringFormat(
            "Не удалось записать members шаблона «{}» (ожидали {} / физ. {}, получили {} / физ. {})",
            normalized, members.size(), expectedPhysical, actualTotal, actualPhysical);
        return false;
    }
    return true;
}

bool GobBlueprintMgr::UpdateFromActiveGroup(Player* player, std::string const& keyOrName, std::string& error)
{
    if (!player)
    {
        error = "Нужен игрок";
        return false;
    }

    GobBlueprintRecord existing;
    if (!ResolveBlueprint(player, keyOrName, true, false, existing, error))
        return false;

    GobGroupRelativeSnapshot snap;
    QuaternionData rootTilt;
    if (!CaptureActiveGroup(player, snap, rootTilt, error))
        return false;
    if (snap.Members.empty())
    {
        error = "Активная группа пуста — нечего записывать в шаблон";
        return false;
    }
    if (snap.Members.size() > GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Активная группа превышает лимит шаблона ({})", GOBGROUP_MAX_MEMBERS);
        return false;
    }

    std::vector<GobBlueprintMember> const members = SnapshotToMembers(snap, rootTilt);
    if (!ReplaceComposition(existing.Id, members, snap.RootGuid, error))
        return false;

    RoleplayDatabase.DirectPExecute(
        "UPDATE gameobject_blueprint SET updated_at = CURRENT_TIMESTAMP WHERE id = {}", existing.Id);
    return true;
}

bool GobBlueprintMgr::Delete(Player* player, std::string const& keyOrName, std::string& error)
{
    GobBlueprintRecord existing;
    if (!ResolveBlueprint(player, keyOrName, true, false, existing, error))
        return false;

    RoleplayDatabase.DirectPExecute("UPDATE gameobject_blueprint SET deleted = 1 WHERE id = {}", existing.Id);
    return true;
}

bool GobBlueprintMgr::Rename(Player* player, std::string const& keyOrName, std::string const& newName, std::string& error)
{
    GobBlueprintRecord existing;
    if (!ResolveBlueprint(player, keyOrName, true, false, existing, error))
        return false;

    std::string normalized = newName;
    if (!NormalizeName(normalized, error))
        return false;
    if (normalized == existing.Name)
        return true;

    std::string const escapedNew = EscapeSql(normalized);
    if (RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint WHERE owner_account_id = {} AND name = '{}' AND deleted = 0 LIMIT 1",
        existing.OwnerAccountId, escapedNew))
    {
        error = Trinity::StringFormat("Шаблон «{}» уже существует у этого владельца", normalized);
        return false;
    }

    RoleplayDatabase.DirectPExecute(
        "UPDATE gameobject_blueprint SET name = '{}' WHERE id = {}",
        escapedNew, existing.Id);
    return true;
}

bool GobBlueprintMgr::SetPublic(Player* player, std::string const& keyOrName, bool isPublic, std::string& error)
{
    GobBlueprintRecord existing;
    if (!ResolveBlueprint(player, keyOrName, true, false, existing, error))
        return false;
    if (!CanSetPublic(player, existing))
    {
        error = "Недостаточно прав для смены публичности шаблона";
        return false;
    }

    RoleplayDatabase.DirectPExecute(
        "UPDATE gameobject_blueprint SET is_public = {} WHERE id = {}",
        isPublic ? 1 : 0, existing.Id);
    return true;
}

bool GobBlueprintMgr::MemberAddObject(Player* player, std::string const& keyOrName,
    ObjectGuid::LowType anchorGroupGuid, ObjectGuid::LowType objectGuid, std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;
    if (bp.PhysicalMemberCount() >= GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Достигнут лимит физических GO ({})", GOBGROUP_MAX_MEMBERS);
        return false;
    }

    ObjectGuid::LowType const anchorRoot = sGobGroupMgr.FindRootGuid(anchorGroupGuid);
    if (!anchorRoot)
    {
        error = Trinity::StringFormat("Опорная группа {} не найдена", anchorGroupGuid);
        return false;
    }
    if (sGobGroupMgr.IsBusy(anchorRoot))
    {
        error = "Опорная группа занята transform";
        return false;
    }

    GameObjectData const* anchorData = sObjectMgr->GetGameObjectData(anchorRoot);
    GameObjectData const* objectData = sObjectMgr->GetGameObjectData(objectGuid);
    if (!anchorData || !objectData)
    {
        error = "Нет GameObjectData для опоры или объекта";
        return false;
    }
    if (anchorData->mapId != objectData->mapId)
    {
        error = "Объект и опорная группа должны быть на одной карте";
        return false;
    }
    if (!ValidateEntry(objectData->id, error))
        return false;

    uint64 const anchorPhase = sRoleplayPhaseMgr.GetSpawnPhaseId(
        RoleplayPhaseSpawnType::GameObject, anchorRoot, anchorData->mapId);
    uint64 const objectPhase = sRoleplayPhaseMgr.GetSpawnPhaseId(
        RoleplayPhaseSpawnType::GameObject, objectGuid, objectData->mapId);
    if (anchorPhase != objectPhase)
    {
        error = "Объект и опорная группа должны быть в одной RP-фазе";
        return false;
    }

    MemberRelativeTransform rel = GobGroupTransform::ComputeRelative(
        anchorData->spawnPoint, anchorData->rotation, objectData->spawnPoint, objectData->rotation);
    if (!GobGroupTransform::IsFinite(Position(rel.OffsetX, rel.OffsetY, rel.OffsetZ, rel.OffsetO))
        || !GobGroupTransform::IsFinite(rel.RelativeRotation))
    {
        error = "Не удалось вычислить relative для объекта";
        return false;
    }

    uint32 sortOrder = 0;
    for (GobBlueprintMember const& m : bp.Members)
        sortOrder = std::max(sortOrder, m.SortOrder + 1);

    RoleplayDatabase.DirectPExecute(
        "INSERT INTO gameobject_blueprint_part (blueprint_id, part_type, label, source_root_guid) "
        "VALUES ({}, 'object', '', {})",
        bp.Id, uint64(objectGuid));
    QueryResult partResult = RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint_part WHERE blueprint_id = {} ORDER BY id DESC LIMIT 1", bp.Id);
    if (!partResult)
    {
        error = "Не удалось создать object-part";
        return false;
    }
    uint32 const partId = partResult->Fetch()[0].GetUInt32();

    RoleplayDatabase.DirectPExecute(
        "INSERT INTO gameobject_blueprint_member "
        "(blueprint_id, part_id, sort_order, is_root, entry, offset_x, offset_y, offset_z, offset_o, "
        "rotation0, rotation1, rotation2, rotation3) VALUES "
        "({}, {}, {}, 0, {}, {}, {}, {}, {}, {}, {}, {}, {})",
        bp.Id, partId, sortOrder, objectData->id,
        GobGroupTransform::FormatSqlFloat(rel.OffsetX),
        GobGroupTransform::FormatSqlFloat(rel.OffsetY),
        GobGroupTransform::FormatSqlFloat(rel.OffsetZ),
        GobGroupTransform::FormatSqlFloat(rel.OffsetO),
        GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.x),
        GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.y),
        GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.z),
        GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.w));
    return true;
}

bool GobBlueprintMgr::MemberAddGroup(Player* player, std::string const& keyOrName,
    ObjectGuid::LowType anchorGroupGuid, ObjectGuid::LowType groupGuid, std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;

    ObjectGuid::LowType const anchorRoot = sGobGroupMgr.FindRootGuid(anchorGroupGuid);
    ObjectGuid::LowType const addRoot = sGobGroupMgr.FindRootGuid(groupGuid);
    if (!anchorRoot || !addRoot)
    {
        error = "Опорная или добавляемая группа не найдена";
        return false;
    }
    if (anchorRoot == addRoot)
    {
        error = "Нельзя добавить опорную группу саму в себя";
        return false;
    }
    if (sGobGroupMgr.IsBusy(anchorRoot) || sGobGroupMgr.IsBusy(addRoot))
    {
        error = "Группа занята transform";
        return false;
    }

    GobGroupRelativeSnapshot addSnap;
    if (!sGobGroupMgr.TryGetRelativeSnapshot(addRoot, addSnap, error))
        return false;

    GameObjectData const* anchorData = sObjectMgr->GetGameObjectData(anchorRoot);
    if (!anchorData)
    {
        error = "Нет GameObjectData у опорного root";
        return false;
    }

    uint32 const incoming = uint32(addSnap.Members.size());
    if (bp.PhysicalMemberCount() + incoming > GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Добавление группы превысит лимит физических GO ({})", GOBGROUP_MAX_MEMBERS);
        return false;
    }

    uint64 const anchorPhase = sRoleplayPhaseMgr.GetSpawnPhaseId(
        RoleplayPhaseSpawnType::GameObject, anchorRoot, anchorData->mapId);

    std::vector<GobBlueprintMember> toInsert;
    toInsert.reserve(addSnap.Members.size());
    uint32 sortOrder = 0;
    for (GobBlueprintMember const& m : bp.Members)
        sortOrder = std::max(sortOrder, m.SortOrder + 1);

    for (GobGroupRelativeMember const& src : addSnap.Members)
    {
        GameObjectData const* data = sObjectMgr->GetGameObjectData(src.SpawnId);
        if (!data)
        {
            error = Trinity::StringFormat("Нет GameObjectData у member {}", src.SpawnId);
            return false;
        }
        if (data->mapId != anchorData->mapId)
        {
            error = "Члены добавляемой группы должны быть на карте опоры";
            return false;
        }
        uint64 const memberPhase = sRoleplayPhaseMgr.GetSpawnPhaseId(
            RoleplayPhaseSpawnType::GameObject, src.SpawnId, data->mapId);
        if (memberPhase != anchorPhase)
        {
            error = "Члены добавляемой группы должны быть в той же RP-фазе";
            return false;
        }
        if (!ValidateEntry(data->id, error))
            return false;

        GobBlueprintMember row;
        row.SortOrder = sortOrder++;
        row.IsRoot = false;
        row.Entry = data->id;
        row.Relative = GobGroupTransform::ComputeRelative(
            anchorData->spawnPoint, anchorData->rotation, data->spawnPoint, data->rotation);
        if (!GobGroupTransform::IsFinite(Position(row.Relative.OffsetX, row.Relative.OffsetY, row.Relative.OffsetZ, row.Relative.OffsetO))
            || !GobGroupTransform::IsFinite(row.Relative.RelativeRotation))
        {
            error = "Не удалось вычислить relative для member группы";
            return false;
        }
        toInsert.push_back(row);
    }

    RoleplayDatabase.DirectPExecute(
        "INSERT INTO gameobject_blueprint_part (blueprint_id, part_type, label, source_root_guid) "
        "VALUES ({}, 'group', '', {})",
        bp.Id, uint64(addRoot));
    QueryResult partResult = RoleplayDatabase.PQuery(
        "SELECT id FROM gameobject_blueprint_part WHERE blueprint_id = {} ORDER BY id DESC LIMIT 1", bp.Id);
    if (!partResult)
    {
        error = "Не удалось создать group-part";
        return false;
    }
    uint32 const partId = partResult->Fetch()[0].GetUInt32();

    RoleplayDatabaseTransaction trans = RoleplayDatabase.BeginTransaction();
    PersistMembers(trans, bp.Id, partId, toInsert);
    RoleplayDatabase.DirectCommitTransaction(trans);
    return true;
}

bool GobBlueprintMgr::MemberRemoveObject(Player* player, std::string const& keyOrName, uint32 memberId, std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;

    GobBlueprintMember const* target = nullptr;
    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.Id == memberId)
        {
            target = &m;
            break;
        }
    }
    if (!target)
    {
        error = Trinity::StringFormat("Member id={} не найден в шаблоне", memberId);
        return false;
    }

    if (target->IsRoot && target->Entry == 0)
    {
        error = "Нельзя удалить виртуальный центр шаблона";
        return false;
    }

    if (target->IsRoot && target->Entry != 0)
    {
        // Convert physical root → virtual center, keep pose/tilt.
        RoleplayDatabase.DirectPExecute(
            "UPDATE gameobject_blueprint_member SET entry = 0 WHERE id = {} AND blueprint_id = {}",
            memberId, bp.Id);
        return true;
    }

    uint32 const partId = target->PartId;
    RoleplayDatabase.DirectPExecute(
        "DELETE FROM gameobject_blueprint_member WHERE id = {} AND blueprint_id = {}",
        memberId, bp.Id);
    CleanupEmptyPart(partId, bp.Id);
    return true;
}

bool GobBlueprintMgr::MemberRemoveGroup(Player* player, std::string const& keyOrName, uint32 partId, std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;

    GobBlueprintPart const* part = nullptr;
    for (GobBlueprintPart const& p : bp.Parts)
    {
        if (p.Id == partId)
        {
            part = &p;
            break;
        }
    }
    if (!part)
    {
        error = Trinity::StringFormat("Part id={} не найден", partId);
        return false;
    }
    if (part->Type != GobBlueprintPartType::Group)
    {
        error = "Удалять можно только part типа group";
        return false;
    }

    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.PartId == partId && m.IsRoot)
        {
            error = "Нельзя удалить part, содержащий root шаблона";
            return false;
        }
    }

    RoleplayDatabase.DirectPExecute(
        "DELETE FROM gameobject_blueprint_member WHERE blueprint_id = {} AND part_id = {}",
        bp.Id, partId);
    RoleplayDatabase.DirectPExecute(
        "DELETE FROM gameobject_blueprint_part WHERE blueprint_id = {} AND id = {}",
        bp.Id, partId);
    return true;
}

bool GobBlueprintMgr::MemberReplace(Player* player, std::string const& keyOrName, uint32 memberId, uint32 newEntry,
    std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;
    if (!ValidateEntry(newEntry, error))
        return false;

    GobBlueprintMember const* target = nullptr;
    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.Id == memberId)
        {
            target = &m;
            break;
        }
    }
    if (!target)
    {
        error = Trinity::StringFormat("Member id={} не найден", memberId);
        return false;
    }
    if (target->Entry == 0)
    {
        error = "Нельзя заменить entry у виртуального центра — используйте setroot/setcenter";
        return false;
    }

    RoleplayDatabase.DirectPExecute(
        "UPDATE gameobject_blueprint_member SET entry = {} WHERE id = {} AND blueprint_id = {}",
        newEntry, memberId, bp.Id);
    return true;
}

bool GobBlueprintMgr::MemberSetRoot(Player* player, std::string const& keyOrName, uint32 memberId, std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;

    size_t newRootIdx = bp.Members.size();
    for (size_t i = 0; i < bp.Members.size(); ++i)
    {
        if (bp.Members[i].Id == memberId)
        {
            newRootIdx = i;
            break;
        }
    }
    if (newRootIdx >= bp.Members.size())
    {
        error = Trinity::StringFormat("Member id={} не найден", memberId);
        return false;
    }
    if (bp.Members[newRootIdx].Entry == 0)
    {
        error = "Корнем может стать только физический member";
        return false;
    }
    if (bp.Members[newRootIdx].IsRoot)
        return true;

    std::vector<SynthPose> poses = SynthesizeWorldPoses(bp.Members);
    if (poses.size() != bp.Members.size())
    {
        error = "Не удалось синтезировать world-pose состава";
        return false;
    }

    SynthPose const& newCenter = poses[newRootIdx];
    QuaternionData const newTilt = GobGroupTransform::ComputeRelativeRotation(
        newCenter.Rot, GobGroupTransform::YawQuat(newCenter.Pos.GetOrientation()));

    std::vector<GobBlueprintMember> rebuilt;
    rebuilt.reserve(bp.Members.size());
    uint32 sort = 1;
    for (size_t i = 0; i < bp.Members.size(); ++i)
    {
        GobBlueprintMember const& src = bp.Members[i];
        // Drop previous virtual center — new physical root becomes the sole root.
        if (src.IsRoot && src.Entry == 0)
            continue;

        GobBlueprintMember row = src;
        if (i == newRootIdx)
        {
            row.IsRoot = true;
            row.SortOrder = 0;
            row.Relative = IdentityRelative();
            row.Relative.RelativeRotation = newTilt;
        }
        else
        {
            row.IsRoot = false;
            row.SortOrder = sort++;
            row.Relative = GobGroupTransform::ComputeRelative(
                newCenter.Pos, newCenter.Rot, poses[i].Pos, poses[i].Rot);
        }
        rebuilt.push_back(row);
    }

    RoleplayDatabaseTransaction trans = RoleplayDatabase.BeginTransaction();
    trans->PAppend(
        "UPDATE gameobject_blueprint_member SET sort_order = sort_order + 100000 WHERE blueprint_id = {}",
        bp.Id);
    for (GobBlueprintMember const& m : rebuilt)
    {
        trans->PAppend(
            "UPDATE gameobject_blueprint_member SET sort_order = {}, is_root = {}, "
            "offset_x = {}, offset_y = {}, offset_z = {}, offset_o = {}, "
            "rotation0 = {}, rotation1 = {}, rotation2 = {}, rotation3 = {} "
            "WHERE id = {} AND blueprint_id = {}",
            m.SortOrder, m.IsRoot ? 1 : 0,
            GobGroupTransform::FormatSqlFloat(m.Relative.OffsetX),
            GobGroupTransform::FormatSqlFloat(m.Relative.OffsetY),
            GobGroupTransform::FormatSqlFloat(m.Relative.OffsetZ),
            GobGroupTransform::FormatSqlFloat(m.Relative.OffsetO),
            GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.x),
            GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.y),
            GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.z),
            GobGroupTransform::FormatSqlFloat(m.Relative.RelativeRotation.w),
            m.Id, bp.Id);
    }
    // Remove old virtual center if it was skipped from rebuilt.
    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.IsRoot && m.Entry == 0)
            trans->PAppend("DELETE FROM gameobject_blueprint_member WHERE id = {} AND blueprint_id = {}", m.Id, bp.Id);
    }
    RoleplayDatabase.DirectCommitTransaction(trans);
    return true;
}

bool GobBlueprintMgr::MemberSetCenter(Player* player, std::string const& keyOrName, bool fromPlayerPose, std::string& error)
{
    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, true, false, bp, error))
        return false;
    if (bp.Members.empty())
    {
        error = "Шаблон пуст";
        return false;
    }

    GobBlueprintMember* root = nullptr;
    for (GobBlueprintMember& m : bp.Members)
    {
        if (m.IsRoot)
        {
            root = &m;
            break;
        }
    }
    if (!root)
    {
        error = "В шаблоне нет root";
        return false;
    }

    if (!fromPlayerPose)
    {
        // Convert physical root to virtual center, keeping current center pose/tilt.
        if (root->Entry == 0)
            return true;
        RoleplayDatabase.DirectPExecute(
            "UPDATE gameobject_blueprint_member SET entry = 0 WHERE id = {} AND blueprint_id = {}",
            root->Id, bp.Id);
        return true;
    }

    if (!player)
    {
        error = "Нужен игрок";
        return false;
    }

    GobGroupRelativeSnapshot snap;
    QuaternionData activeTilt;
    if (!CaptureActiveGroup(player, snap, activeTilt, error))
        return false;

    GameObjectData const* activeRootData = sObjectMgr->GetGameObjectData(snap.RootGuid);
    if (!activeRootData)
    {
        error = "Нет GameObjectData у активной группы";
        return false;
    }

    // Current blueprint center world pose = active group root (anchor of the template in world).
    Position const oldCenterPos = activeRootData->spawnPoint;
    QuaternionData const oldCenterRot = activeRootData->rotation;

    // New virtual center at player with identity tilt.
    Position const newCenterPos = *player;
    QuaternionData const newCenterRot = GobGroupTransform::YawQuat(player->GetOrientation());

    uint32 const oldRootEntry = root->Entry;
    uint32 const oldRootPartId = root->PartId;
    MemberRelativeTransform demotedRel;
    bool const demotePhysicalRoot = oldRootEntry != 0;
    if (demotePhysicalRoot)
        demotedRel = GobGroupTransform::ComputeRelative(newCenterPos, newCenterRot, oldCenterPos, oldCenterRot);

    RoleplayDatabaseTransaction trans = RoleplayDatabase.BeginTransaction();
    trans->PAppend(
        "UPDATE gameobject_blueprint_member SET sort_order = sort_order + 100000 WHERE blueprint_id = {}",
        bp.Id);

    // Existing root row becomes virtual center at player (identity tilt).
    trans->PAppend(
        "UPDATE gameobject_blueprint_member SET entry = 0, sort_order = 0, is_root = 1, "
        "offset_x = 0, offset_y = 0, offset_z = 0, offset_o = 0, "
        "rotation0 = 0, rotation1 = 0, rotation2 = 0, rotation3 = 1 "
        "WHERE id = {} AND blueprint_id = {}",
        root->Id, bp.Id);

    uint32 sort = 1;
    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.IsRoot || m.Entry == 0)
            continue;

        Position const memberPos = MemberWorldPos(oldCenterPos, oldCenterRot, m.Relative);
        QuaternionData const memberRot = MemberWorldRot(oldCenterRot, m.Relative);
        MemberRelativeTransform const rel = GobGroupTransform::ComputeRelative(
            newCenterPos, newCenterRot, memberPos, memberRot);

        trans->PAppend(
            "UPDATE gameobject_blueprint_member SET sort_order = {}, is_root = 0, "
            "offset_x = {}, offset_y = {}, offset_z = {}, offset_o = {}, "
            "rotation0 = {}, rotation1 = {}, rotation2 = {}, rotation3 = {} "
            "WHERE id = {} AND blueprint_id = {}",
            sort++,
            GobGroupTransform::FormatSqlFloat(rel.OffsetX),
            GobGroupTransform::FormatSqlFloat(rel.OffsetY),
            GobGroupTransform::FormatSqlFloat(rel.OffsetZ),
            GobGroupTransform::FormatSqlFloat(rel.OffsetO),
            GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.x),
            GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.y),
            GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.z),
            GobGroupTransform::FormatSqlFloat(rel.RelativeRotation.w),
            m.Id, bp.Id);
    }

    // Former physical root becomes a new non-root member (new DB row).
    if (demotePhysicalRoot)
    {
        trans->PAppend(
            "INSERT INTO gameobject_blueprint_member "
            "(blueprint_id, part_id, sort_order, is_root, entry, offset_x, offset_y, offset_z, offset_o, "
            "rotation0, rotation1, rotation2, rotation3) VALUES "
            "({}, {}, {}, 0, {}, {}, {}, {}, {}, {}, {}, {}, {})",
            bp.Id, oldRootPartId, sort,
            oldRootEntry,
            GobGroupTransform::FormatSqlFloat(demotedRel.OffsetX),
            GobGroupTransform::FormatSqlFloat(demotedRel.OffsetY),
            GobGroupTransform::FormatSqlFloat(demotedRel.OffsetZ),
            GobGroupTransform::FormatSqlFloat(demotedRel.OffsetO),
            GobGroupTransform::FormatSqlFloat(demotedRel.RelativeRotation.x),
            GobGroupTransform::FormatSqlFloat(demotedRel.RelativeRotation.y),
            GobGroupTransform::FormatSqlFloat(demotedRel.RelativeRotation.z),
            GobGroupTransform::FormatSqlFloat(demotedRel.RelativeRotation.w));
    }

    RoleplayDatabase.DirectCommitTransaction(trans);
    return true;
}

bool GobBlueprintMgr::Spawn(Player* player, std::string const& keyOrName, std::string& error)
{
    if (!player || !player->GetSession() || !player->GetMap())
    {
        error = "Нужен игрок";
        return false;
    }

    uint32 const accountId = player->GetSession()->GetAccountId();
    {
        std::scoped_lock lock(_mutex);
        if (HasBusySpawn(accountId))
        {
            error = "Для этого аккаунта уже идёт постановка шаблона";
            return false;
        }
    }

    GobBlueprintRecord bp;
    if (!ResolveBlueprint(player, keyOrName, false, true, bp, error))
        return false;
    if (!CanSpawn(player, bp))
    {
        error = "Нет прав на постановку этого шаблона";
        return false;
    }
    if (bp.PhysicalMemberCount() < 1)
    {
        error = "В шаблоне нет физических GO";
        return false;
    }
    if (bp.PhysicalMemberCount() > GOBGROUP_MAX_MEMBERS)
    {
        error = Trinity::StringFormat("Шаблон превышает лимит ({})", GOBGROUP_MAX_MEMBERS);
        return false;
    }

    uint64 const phaseId = sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId());
    bool const staffAccess = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
    bool const serverStaff = sRoleplayPhaseMgr.CanMutateCommonWorld(player->GetSession()->GetSecurity());
    if (!phaseId && !serverStaff)
    {
        error = "Постоянные объекты в общем мире может ставить только GM1+";
        return false;
    }
    if (phaseId && !sRoleplayPhaseMgr.CanEdit(phaseId, player->GetGUID().GetCounter(),
        player->GetSession()->GetAccountId(), staffAccess, serverStaff))
    {
        error = "Для постановки в этой RP-фазе нужны права editor/manager/owner";
        return false;
    }

    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.Entry == 0)
            continue;
        if (!ValidateEntry(m.Entry, error))
            return false;
    }

    GobBlueprintMember const* rootMember = nullptr;
    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.IsRoot)
        {
            rootMember = &m;
            break;
        }
    }
    if (!rootMember)
    {
        error = "В шаблоне нет root";
        return false;
    }

    bool const virtualRoot = (rootMember->Entry == 0);
    QuaternionData const rootTilt = rootMember->Relative.RelativeRotation;
    Position const playerPos = *player;
    QuaternionData const playerYaw = GobGroupTransform::YawQuat(player->GetOrientation());
    QuaternionData const centerRot = GobGroupTransform::ApplyRelativeRotation(rootTilt, playerYaw);
    Position centerPos = playerPos;
    centerPos.SetOrientation(player->GetOrientation());

    auto job = std::make_unique<SpawnJob>();
    job->AccountId = accountId;
    job->BnetAccountId = GetBnetAccountId(player);
    job->PlayerGuid = player->GetGUID();
    job->MapId = player->GetMapId();
    job->Difficulty = player->GetMap()->GetDifficultyID();
    job->PhaseId = phaseId;
    job->BlueprintKey = bp.Key();
    job->BlueprintName = bp.Name;
    job->GroupName = bp.Name;
    job->QueuedAtMs = getMSTime();
    job->Staged.reserve(bp.PhysicalMemberCount());

    for (GobBlueprintMember const& m : bp.Members)
    {
        if (m.Entry == 0)
            continue;

        StagedSpawn staged;
        staged.Entry = m.Entry;
        staged.SpawnId = sObjectMgr->GenerateGameObjectSpawnId();
        staged.RelativeToRoot = m.Relative;

        if (m.IsRoot)
        {
            staged.IsRoot = true;
            staged.WorldPos = centerPos;
            staged.WorldRot = centerRot;
            staged.RelativeToRoot = IdentityRelative();
            job->RootGuid = staged.SpawnId;
        }
        else
        {
            staged.IsRoot = false;
            staged.WorldPos = MemberWorldPos(centerPos, centerRot, m.Relative);
            staged.WorldRot = MemberWorldRot(centerRot, m.Relative);
        }

        job->Staged.push_back(staged);
    }

    if (job->Staged.empty())
    {
        error = "Нечего ставить — нет физических members";
        return false;
    }

    if (virtualRoot)
    {
        // First physical staged becomes GobGroup root; recompute relatives from world poses.
        StagedSpawn& groupRoot = job->Staged.front();
        groupRoot.IsRoot = true;
        groupRoot.RelativeToRoot = IdentityRelative();
        job->RootGuid = groupRoot.SpawnId;

        for (size_t i = 1; i < job->Staged.size(); ++i)
        {
            StagedSpawn& s = job->Staged[i];
            s.IsRoot = false;
            s.RelativeToRoot = GobGroupTransform::ComputeRelative(
                groupRoot.WorldPos, groupRoot.WorldRot, s.WorldPos, s.WorldRot);
        }
    }
    else if (!job->RootGuid)
    {
        error = "Не удалось назначить root спавна";
        for (StagedSpawn const& s : job->Staged)
            sObjectMgr->DeleteGameObjectData(s.SpawnId);
        return false;
    }

    for (StagedSpawn const& s : job->Staged)
        FillStagedGameObjectData(s.SpawnId, s.Entry, job->MapId, job->Difficulty, s.WorldPos, s.WorldRot);

    job->SqlChunks = uint32((job->Staged.size() + GOBGROUP_SQL_CHUNK_SIZE - 1) / GOBGROUP_SQL_CHUNK_SIZE);
    if (!StartWorldTransaction(*job))
    {
        error = job->Error.empty() ? "Не удалось начать world-транзакцию спавна" : job->Error;
        for (StagedSpawn const& s : job->Staged)
            sObjectMgr->DeleteGameObjectData(s.SpawnId);
        return false;
    }

    std::scoped_lock lock(_mutex);
    _spawnJobs[accountId] = std::move(job);
    return true;
}

bool GobBlueprintMgr::StartWorldTransaction(SpawnJob& job)
{
    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();

    for (size_t offset = 0; offset < job.Staged.size(); offset += GOBGROUP_SQL_CHUNK_SIZE)
    {
        size_t const end = std::min(offset + GOBGROUP_SQL_CHUNK_SIZE, job.Staged.size());
        for (size_t i = offset; i < end; ++i)
        {
            StagedSpawn const& s = job.Staged[i];
            AppendGameObjectInsert(trans, s.SpawnId, s.Entry, job.MapId, job.Difficulty, s.WorldPos, s.WorldRot);
        }
    }

    std::string escapedName = job.GroupName;
    WorldDatabase.EscapeString(escapedName);
    trans->PAppend(
        "INSERT INTO gameobject_group_root (root_guid, name, created_by) VALUES ({}, '{}', {})",
        job.RootGuid, escapedName, job.AccountId);

    std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> relativeRows;
    relativeRows.reserve(job.Staged.size() > 1 ? job.Staged.size() - 1 : 0);
    for (StagedSpawn const& s : job.Staged)
    {
        if (s.IsRoot)
            continue;
        relativeRows.emplace_back(s.SpawnId, s.RelativeToRoot);
    }
    if (!relativeRows.empty())
        GobGroupTransform::AppendRelativeUpsertChunks(trans, job.RootGuid, relativeRows);

    job.Phase = SpawnPhase::WorldPending;
    job.DbStartMs = getMSTime();

    uint32 const accountId = job.AccountId;
    TransactionCallback callback = WorldDatabase.AsyncCommitTransaction(trans);
    // AfterComplete runs from Update() while _mutex is already held — do not lock again
    // and do not call DirectCommit / map callbacks here (would deadlock or stall world).
    callback.AfterComplete([accountId](bool success)
    {
        GobBlueprintMgr& mgr = GobBlueprintMgr::Instance();
        auto it = mgr._spawnJobs.find(accountId);
        if (it == mgr._spawnJobs.end() || !it->second)
            return;

        SpawnJob& active = *it->second;
        active.DbElapsedMs = GetMSTimeDiffToNow(active.DbStartMs);
        if (!success)
        {
            active.Error = "Ошибка записи World DB";
            active.Phase = SpawnPhase::Failed;
            for (StagedSpawn const& s : active.Staged)
                sObjectMgr->DeleteGameObjectData(s.SpawnId);
            GobGroupBatch::FinalizeTelemetry(active.Telemetry, uint32(active.Staged.size()),
                active.SqlChunks, active.DbElapsedMs, 0, 0, GobGroupBatchTelemetry::Result::Failed);
            mgr.NotifySpawnResult(active, false);
            return;
        }

        active.Phase = SpawnPhase::RoleplayPending;
    });
    job.WorldCallback = std::move(callback);
    return true;
}

void GobBlueprintMgr::ContinueSpawnAfterWorld(uint32 accountId)
{
    SpawnJob* job = nullptr;
    {
        std::scoped_lock lock(_mutex);
        auto it = _spawnJobs.find(accountId);
        if (it == _spawnJobs.end() || !it->second)
            return;
        if (it->second->Phase != SpawnPhase::RoleplayPending)
            return;
        job = it->second.get();
    }

    // Roleplay/world compensation and runtime schedule run without holding _mutex.
    if (!CommitRoleplay(*job))
    {
        CompensateWorld(*job);
        NotifySpawnResult(*job, false);
        return;
    }

    {
        std::scoped_lock lock(_mutex);
        auto it = _spawnJobs.find(accountId);
        if (it == _spawnJobs.end() || !it->second || it->second.get() != job)
            return;
        if (job->Phase != SpawnPhase::RoleplayPending)
            return;
        job->Phase = SpawnPhase::RuntimePending;
    }

    ScheduleRuntimePublish(*job);
}

bool GobBlueprintMgr::CommitRoleplay(SpawnJob& job)
{
    RoleplayDatabaseTransaction trans = RoleplayDatabase.BeginTransaction();

    for (size_t offset = 0; offset < job.Staged.size(); offset += GOBGROUP_SQL_CHUNK_SIZE)
    {
        size_t const end = std::min(offset + GOBGROUP_SQL_CHUNK_SIZE, job.Staged.size());
        for (size_t i = offset; i < end; ++i)
        {
            RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_REP_GAMEOBJECTEXTRA);
            stmt->setUInt64(0, job.Staged[i].SpawnId);
            stmt->setUInt32(1, job.BnetAccountId);
            stmt->setUInt64(2, job.PlayerGuid.GetCounter());
            trans->Append(stmt);
        }
    }

    // Direct commit so we can compensate World immediately on failure.
    RoleplayDatabase.DirectCommitTransaction(trans);

    if (job.PhaseId)
    {
        std::vector<uint64> spawnIds;
        spawnIds.reserve(job.Staged.size());
        for (StagedSpawn const& s : job.Staged)
            spawnIds.push_back(s.SpawnId);

        if (!sRoleplayPhaseMgr.AssignPersistentSpawns(RoleplayPhaseSpawnType::GameObject, spawnIds, job.PhaseId))
        {
            job.Error = "Не удалось назначить RP-фазу поставленным GO";
            return false;
        }
    }

    sRoleplayPhaseMgr.WriteAudit(job.PlayerGuid.GetCounter(), job.AccountId,
        "gobject_blueprint_spawn", job.PhaseId,
        Trinity::StringFormat(R"({{"key":"{}","blueprint":"{}","root":{},"count":{}}})",
            job.BlueprintKey, job.BlueprintName, job.RootGuid, job.Staged.size()));
    return true;
}

void GobBlueprintMgr::CompensateWorld(SpawnJob& job)
{
    WorldDatabaseTransaction trans = WorldDatabase.BeginTransaction();
    for (StagedSpawn const& s : job.Staged)
    {
        WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_DEL_GAMEOBJECT);
        stmt->setUInt64(0, s.SpawnId);
        trans->Append(stmt);
    }
    trans->PAppend("DELETE FROM gameobject_group WHERE root_guid = {}", job.RootGuid);
    trans->PAppend("DELETE FROM gameobject_group_root WHERE root_guid = {}", job.RootGuid);
    WorldDatabase.DirectCommitTransaction(trans);

    for (StagedSpawn const& s : job.Staged)
    {
        RoleplayDatabasePreparedStatement* stmt = RoleplayDatabase.GetPreparedStatement(Roleplay_DEL_GAMEOBJECTEXTRA);
        stmt->setUInt64(0, s.SpawnId);
        RoleplayDatabase.Execute(stmt);
        sObjectMgr->DeleteGameObjectData(s.SpawnId);
    }

    job.Phase = SpawnPhase::Compensated;
    if (job.Error.empty())
        job.Error = "Ошибка Roleplay commit; world-спавн откатан";
    GobGroupBatch::FinalizeTelemetry(job.Telemetry, uint32(job.Staged.size()), job.SqlChunks,
        job.DbElapsedMs, 0, 0, GobGroupBatchTelemetry::Result::Compensated);
}

void GobBlueprintMgr::NotifySpawnResult(SpawnJob& job, bool ok)
{
    Player* player = ObjectAccessor::FindPlayer(job.PlayerGuid);
    if (!player)
        return;

    if (ok)
    {
        ChatHandler(player->GetSession()).PSendSysMessage(
            "Поставлен шаблон «%s» как группа %u — |cffffffff|Hgameobject:%u|h[%u]|h|r. "
            "Возьмите группу: .gobject group use %u",
            job.BlueprintName.c_str(), uint32(job.RootGuid), uint32(job.RootGuid),
            uint32(job.RootGuid), uint32(job.RootGuid));
        GobGroupProtocol::SendBlueprintResult(player, "spawn", "ok",
            Trinity::StringFormat("{}:{}", job.BlueprintKey, job.RootGuid));
        GobGroupProtocol::SendBlueprintStatus(player,
            Trinity::StringFormat("Готово: группа {}", job.RootGuid));
    }
    else
    {
        std::string const msg = job.Error.empty() ? "Ошибка постановки шаблона" : job.Error;
        ChatHandler(player->GetSession()).SendSysMessage(msg.c_str());
        GobGroupProtocol::SendBlueprintResult(player, "spawn", "error", msg);
        GobGroupProtocol::SendBlueprintStatus(player, Trinity::StringFormat("Ошибка: {}", msg));
    }
}

void GobBlueprintMgr::ScheduleRuntimePublish(SpawnJob& job)
{
    Map* map = sMapMgr->FindMap(job.MapId, 0);
    if (!map)
    {
        // Map unloaded: publish group cache only; GO appear on grid load.
        std::vector<ObjectGuid::LowType> ordered;
        std::unordered_map<ObjectGuid::LowType, MemberRelativeTransform> relatives;
        ordered.reserve(job.Staged.size());
        for (StagedSpawn const& s : job.Staged)
        {
            ordered.push_back(s.SpawnId);
            if (!s.IsRoot)
                relatives[s.SpawnId] = s.RelativeToRoot;
            sObjectMgr->AddGameobjectToGrid(const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(s.SpawnId)));
        }

        Player* player = ObjectAccessor::FindPlayer(job.PlayerGuid);
        std::string publishError;
        if (player)
            sGobGroupMgr.PublishSpawnedGroup(player, job.RootGuid, job.GroupName, ordered, relatives, publishError);
        else
            publishError = "Игрок оффлайн во время публикации шаблона";

        job.Phase = publishError.empty() ? SpawnPhase::Completed : SpawnPhase::Failed;
        job.Error = publishError;
        GobGroupBatch::FinalizeTelemetry(job.Telemetry, uint32(job.Staged.size()), job.SqlChunks,
            job.DbElapsedMs, 0, 0,
            publishError.empty() ? GobGroupBatchTelemetry::Result::Completed : GobGroupBatchTelemetry::Result::Failed);
        NotifySpawnResult(job, publishError.empty());
        return;
    }

    uint32 const accountId = job.AccountId;
    map->AddFarSpellCallback([accountId](Map* mapCtx)
    {
        GobBlueprintMgr::Instance().ProcessRuntimePublish(mapCtx, accountId);
    });
}

void GobBlueprintMgr::ProcessRuntimePublish(Map* map, uint32 accountId)
{
    if (!map)
        return;

    std::scoped_lock lock(_mutex);
    auto it = _spawnJobs.find(accountId);
    if (it == _spawnJobs.end() || !it->second)
        return;

    SpawnJob& job = *it->second;
    if (job.Phase != SpawnPhase::RuntimePending)
        return;

    uint32 const startMs = getMSTime();
    std::vector<ObjectGuid::LowType> ordered;
    std::unordered_map<ObjectGuid::LowType, MemberRelativeTransform> relatives;
    ordered.reserve(job.Staged.size());
    for (StagedSpawn const& s : job.Staged)
    {
        ordered.push_back(s.SpawnId);
        if (!s.IsRoot)
            relatives[s.SpawnId] = s.RelativeToRoot;
    }

    std::string runtimeError;
    GobGroupBatch::SpawnNew(map, ordered, runtimeError);

    Player* player = ObjectAccessor::FindPlayer(job.PlayerGuid);
    std::string publishError;
    if (player)
    {
        if (!sGobGroupMgr.PublishSpawnedGroup(player, job.RootGuid, job.GroupName, ordered, relatives, publishError))
            runtimeError = publishError.empty() ? "Не удалось опубликовать группу" : publishError;
        else
            player->SetLastTargetedGO(job.RootGuid);
    }
    else if (runtimeError.empty())
        runtimeError = "Игрок оффлайн во время публикации шаблона";

    job.RuntimeElapsedMs = GetMSTimeDiffToNow(startMs);
    job.Error = runtimeError;
    job.Phase = runtimeError.empty() ? SpawnPhase::Completed : SpawnPhase::Failed;
    uint32 const queueWait = job.QueuedAtMs ? getMSTimeDiff(job.QueuedAtMs, startMs) : 0;
    GobGroupBatch::FinalizeTelemetry(job.Telemetry, uint32(job.Staged.size()), job.SqlChunks,
        job.DbElapsedMs, job.RuntimeElapsedMs, queueWait,
        runtimeError.empty() ? GobGroupBatchTelemetry::Result::Completed : GobGroupBatchTelemetry::Result::Failed);

    if (!runtimeError.empty())
        TC_LOG_ERROR("scripts", "GobBlueprint spawn runtime issue for account {}: {}", accountId, runtimeError);

    NotifySpawnResult(job, runtimeError.empty());
}

void GobBlueprintMgr::Update(uint32 /*diff*/)
{
    std::vector<uint32> roleplayReady;
    {
        std::scoped_lock lock(_mutex);
        for (auto& [accountId, jobPtr] : _spawnJobs)
        {
            if (!jobPtr)
                continue;

            if (jobPtr->WorldCallback && jobPtr->Phase == SpawnPhase::WorldPending)
            {
                if (jobPtr->WorldCallback->InvokeIfReady())
                    jobPtr->WorldCallback.reset();
            }

            if (jobPtr->Phase == SpawnPhase::RoleplayPending)
                roleplayReady.push_back(accountId);
        }
    }

    for (uint32 accountId : roleplayReady)
        ContinueSpawnAfterWorld(accountId);
}

std::string GobBlueprintMgr::BuildStatus(Player* player) const
{
    if (!player || !player->GetSession())
        return "Нет задачи";

    std::scoped_lock lock(_mutex);
    auto it = _spawnJobs.find(player->GetSession()->GetAccountId());
    if (it == _spawnJobs.end() || !it->second)
        return "Нет задачи";

    SpawnJob const& job = *it->second;
    switch (job.Phase)
    {
        case SpawnPhase::WorldPending:
        case SpawnPhase::RoleplayPending:
            return "Создание шаблона…";
        case SpawnPhase::RuntimePending:
            return "Создание группы…";
        case SpawnPhase::Completed:
            return Trinity::StringFormat("Готово: группа {}", job.RootGuid);
        case SpawnPhase::Failed:
        case SpawnPhase::Compensated:
            return Trinity::StringFormat("Ошибка: {}",
                job.Error.empty() ? "неизвестная ошибка" : job.Error);
        case SpawnPhase::None:
        default:
            return "Нет задачи";
    }
}
}