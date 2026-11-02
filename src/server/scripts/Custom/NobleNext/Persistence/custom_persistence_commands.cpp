/* NobleNext GM persistence. Uses public core APIs; no core save-path overrides. */

#include "ScriptMgr.h"
#include "noble_next_bake.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "RBAC.h"
#include "RolePlay.h"
#include "RoleplayCommandPhaseGuard.h"
#include "WaypointManager.h"
#include "WorldSession.h"
#include <limits>
#include <set>

namespace RoleplayCore::NobleNext
{
namespace
{
using namespace Trinity::ChatCommands;

// Local statements stay with the extension instead of changing WorldDatabase.
constexpr char NextPathIdSql[] = "SELECT GREATEST(COALESCE((SELECT MAX(PathId) FROM waypoint_path), 0), COALESCE((SELECT MAX(PathId) FROM waypoint_path_node), 0)) + 1, 1";
constexpr char NextNodeIdSql[] = "SELECT PathId, COALESCE((SELECT MAX(NodeId) FROM waypoint_path_node WHERE PathId = {}), 0) + 1 FROM waypoint_path WHERE PathId = {}";
constexpr char NewPathSql[] = "INSERT INTO waypoint_path (PathId, MoveType, Flags, Velocity, Comment) VALUES ({}, 0, 0, NULL, 'NobleNext')";
constexpr char AddNodeSql[] = "INSERT INTO waypoint_path_node (PathId, NodeId, PositionX, PositionY, PositionZ, Orientation) VALUES ({}, {}, {}, {}, {}, {})";
constexpr char SetPhaseSql[] = "UPDATE {} SET PhaseId = {}, PhaseGroup = {} WHERE guid = {} AND map = {}";
QueryCallbackProcessor Queries;
AsyncCallbackProcessor<TransactionCallback> Commits;
std::set<std::pair<RoleplayPhaseSpawnType, uint64>> PhaseWrites;

class PersistenceCommands : public CommandScript
{
public:
    PersistenceCommands() : CommandScript("noble_next_persistence_commands") { }

    std::span<ChatCommandBuilder const> GetCommands() const override
    {
        static ChatCommandTable persist =
        {
            { "npc-phase", HandleNpcPhase, rbac::RBAC_PERM_COMMAND_NPC_SET_PHASE, Console::No },
            { "npc-phasegroup", HandleNpcPhaseGroup, rbac::RBAC_PERM_COMMAND_NPC_SET_PHASE, Console::No },
            { "go-phase", HandleGoPhase, rbac::RBAC_PERM_COMMAND_GOBJECT_SET_PHASE, Console::No },
            { "wp-add", HandleWpAdd, rbac::RBAC_PERM_COMMAND_WP_ADD, Console::No },
            { "cnpc-spawn", HandleCustomNpcSpawn, rbac::RBAC_PERM_COMMAND_CUSTOMNPC_SPAWN, Console::No },
            { "npc-bake", HandleNpcBake, rbac::RBAC_PERM_COMMAND_NPC_ADD, Console::No },
            { "go-bake", HandleGoBake, rbac::RBAC_PERM_COMMAND_GOBJECT_ADD, Console::No },
            { "go-list", HandleGoBakeList, rbac::RBAC_PERM_COMMAND_GOBJECT_ADD, Console::No },
            { "wp-bake", HandleRouteBake, rbac::RBAC_PERM_COMMAND_WP_ADD, Console::No },
        };
        static ChatCommandTable rps = { { "persist", persist } };
        static ChatCommandTable commands = { { "rps", rps } };
        return commands;
    }

private:
    static bool Fail(ChatHandler* handler, std::string_view message)
    {
        handler->SendSysMessage(message);
        handler->SetSentErrorMessage(true);
        return false;
    }

    static void Reply(ObjectGuid author, std::string_view message)
    {
        if (Player* player = ObjectAccessor::FindConnectedPlayer(author))
            ChatHandler(player->GetSession()).SendSysMessage(message);
    }

    static bool SavePhase(ChatHandler* handler, WorldObject* object, RoleplayPhaseSpawnType type,
        uint64 spawnId, uint32 phaseId, uint32 phaseGroup)
    {
        Player* player = handler->GetPlayer();
        if (!object || !spawnId)
            return Fail(handler, "Выберите постоянный спавн на текущей карте.");
        if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, handler, type, spawnId,
            false, "persist_native_phase"))
            return false;
        if (phaseId > std::numeric_limits<uint16>::max() || (phaseId && !sPhaseStore.LookupEntry(phaseId))
            || (phaseGroup && !sDB2Manager.GetPhasesForGroup(phaseGroup)))
            return Fail(handler, "Штатная фаза или группа фаз не найдена.");
        if (!PhaseWrites.emplace(type, spawnId).second)
            return Fail(handler, "Дождитесь подтверждения предыдущего сохранения этого спавна.");

        char const* table = type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject";
        uint32 const mapId = player->GetMapId();
        uint32 const instanceId = player->GetInstanceId();
        ObjectGuid const objectGuid = object->GetGUID();
        ObjectGuid const authorGuid = player->GetGUID();
        auto transaction = WorldDatabase.BeginTransaction();
        transaction->PAppend(SetPhaseSql,
            table, phaseId, phaseGroup, spawnId, mapId);
        auto commit = WorldDatabase.AsyncCommitTransaction(transaction);
        commit.AfterComplete(
            [authorGuid, objectGuid, mapId, instanceId, type, spawnId, phaseId, phaseGroup](bool success)
        {
            if (!success)
            {
                PhaseWrites.erase({ type, spawnId });
                Reply(authorGuid, "Фаза не сохранена: ошибка записи в БД.");
                return;
            }
            // A successful commit can update zero rows if an external operation deleted the spawn.
            char const* table = type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject";
            Queries.AddCallback(WorldDatabase.AsyncQuery(Trinity::StringFormat(
                "SELECT PhaseId, PhaseGroup FROM {} WHERE guid = {} AND map = {}", table, spawnId, mapId).c_str()))
                .WithCallback([authorGuid, objectGuid, mapId, instanceId, type, spawnId, phaseId, phaseGroup](QueryResult result)
            {
                PhaseWrites.erase({ type, spawnId });
                if (!result || (*result)[0].GetUInt32() != phaseId || (*result)[1].GetUInt32() != phaseGroup)
                {
                    Reply(authorGuid, "Запись фазы не подтверждена. Перечитайте спавн перед повтором.");
                    return;
                }
                SpawnData* data = type == RoleplayPhaseSpawnType::Creature
                    ? static_cast<SpawnData*>(const_cast<CreatureData*>(sObjectMgr->GetCreatureData(spawnId)))
                    : static_cast<SpawnData*>(const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(spawnId)));
                if (!data || data->mapId != mapId)
                    return;
                data->phaseId = phaseId;
                data->phaseGroup = phaseGroup;
                WorldObject* live = nullptr;
                if (Map* map = sMapMgr->FindMap(mapId, instanceId))
                    live = type == RoleplayPhaseSpawnType::Creature
                        ? static_cast<WorldObject*>(map->GetCreature(objectGuid))
                        : static_cast<WorldObject*>(map->GetGameObject(objectGuid));
                if (live && live->GetGUID() == objectGuid)
                {
                    // Do not leave a stale explicit override for a later native SaveToDB call.
                    live->SetDBPhase(0);
                    PhasingHandler::InitDbPhaseShift(live->GetPhaseShift(), data->phaseUseFlags, phaseId, phaseGroup);
                    live->UpdateObjectVisibility();
                }
                Reply(authorGuid, "Фаза сохранена в БД.");
            });
        });
        Commits.AddCallback(std::move(commit));
        handler->SendSysMessage("Сохранение фазы: ожидайте подтверждения в чате.");
        return true;
    }

    static bool HandleNpcPhase(ChatHandler* handler, uint32 phaseId)
    {
        Creature* creature = handler->getSelectedCreature();
        return SavePhase(handler, creature, RoleplayPhaseSpawnType::Creature,
            creature ? creature->GetSpawnId() : 0, phaseId, 0);
    }

    static bool HandleNpcPhaseGroup(ChatHandler* handler, uint32 phaseGroup)
    {
        Creature* creature = handler->getSelectedCreature();
        return SavePhase(handler, creature, RoleplayPhaseSpawnType::Creature,
            creature ? creature->GetSpawnId() : 0, 0, phaseGroup);
    }

    static bool HandleGoPhase(ChatHandler* handler, uint64 spawnId, uint32 phaseId)
    {
        return SavePhase(handler, handler->GetObjectFromPlayerMapByDbGuid(spawnId),
            RoleplayPhaseSpawnType::GameObject, spawnId, phaseId, 0);
    }

    static bool HandleWpAdd(ChatHandler* handler, Optional<uint32> pathId)
    {
        Creature* target = handler->getSelectedCreature();
        bool const newPath = !pathId && !target;
        if (!pathId && target)
            pathId = target->GetWaypointPathId();
        if (target && target->GetSpawnId() && !RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(
            handler->GetPlayer(), handler, RoleplayPhaseSpawnType::Creature, target->GetSpawnId(), false, "persist_wp_add"))
            return false;
        if (!newPath && (!pathId || !*pathId))
            return Fail(handler, "У существа нет пути. Снимите выделение для создания нового пути.");
        Player* player = handler->GetPlayer();
        auto query = newPath ? WorldDatabase.AsyncQuery(NextPathIdSql)
            : WorldDatabase.AsyncQuery(Trinity::StringFormat(NextNodeIdSql, *pathId, *pathId).c_str());
        Queries.AddCallback(std::move(query)).WithCallback(
            [authorGuid = player->GetGUID(), mapId = player->GetMapId(), instanceId = player->GetInstanceId(),
                position = player->GetPosition(), newPath](QueryResult result)
        {
            if (!result || (*result)[0].GetUInt64() > std::numeric_limits<uint32>::max()
                || (*result)[1].GetUInt64() > std::numeric_limits<uint32>::max())
            {
                Reply(authorGuid, "Путь не найден или закончились ID. Точка не сохранена.");
                return;
            }
            uint32 const path = uint32((*result)[0].GetUInt64());
            uint32 const node = uint32((*result)[1].GetUInt64());
            auto transaction = WorldDatabase.BeginTransaction();
            if (newPath)
                transaction->PAppend(NewPathSql, path);
            transaction->PAppend(AddNodeSql,
                path, node, position.GetPositionX(), position.GetPositionY(), position.GetPositionZ(), position.GetOrientation());
            auto commit = WorldDatabase.AsyncCommitTransaction(transaction);
            commit.AfterComplete(
                [authorGuid, mapId, instanceId, path, node](bool success)
            {
                if (!success)
                {
                    Reply(authorGuid, "Точка не сохранена: ошибка БД или конфликт ID. Повторите команду.");
                    return;
                }
                Player* author = ObjectAccessor::FindConnectedPlayer(authorGuid);
                if (author && author->GetMapId() == mapId && author->GetInstanceId() == instanceId)
                {
                    if (WaypointPath const* oldPath = sWaypointMgr->GetPath(path))
                        sWaypointMgr->DevisualizePath(author, oldPath);
                    sWaypointMgr->ReloadPath(path);
                }
                Reply(authorGuid, Trinity::StringFormat("Путь {}, точка {} сохранены в БД. Для загрузки пути: .wp reload {}", path, node, path));
            });
            Commits.AddCallback(std::move(commit));
        });
        handler->SendSysMessage("Сохранение точки: ожидайте подтверждения в чате.");
        return true;
    }

    static bool HandleCustomNpcSpawn(ChatHandler* handler, std::string const& key)
    {
        if (!sRoleplay->CustomNpcNameExists(key))
            return Fail(handler, "Кастомный NPC не найден.");
        // Native .npc add already owns spawn creation, RP ACL and phase assignment.
        // Keep those rules in one place instead of copying .cnpc spawn into this module.
        if (!handler->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_NPC_ADD))
            return Fail(handler, "Для постоянного спавна требуется разрешение .npc add.");
        return handler->_ParseCommands(Trinity::StringFormat("npc add {}", sRoleplay->GetEntryIdForNpc(key)))
            && !handler->HasSentErrorMessage();
    }
};

class PersistenceCallbacks : public WorldScript
{
public:
    PersistenceCallbacks() : WorldScript("noble_next_persistence_callbacks") { }
    void OnUpdate(uint32 diff) override
    {
        Queries.ProcessReadyCallbacks();
        Commits.ProcessReadyCallbacks();
        UpdateBakes(diff);
    }
    void OnStartup() override { LoadBakedRoutes(); }
};
}
}

void AddSC_NobleNextPersistenceCommands()
{
    new RoleplayCore::NobleNext::PersistenceCommands();
    new RoleplayCore::NobleNext::PersistenceCallbacks();
}
