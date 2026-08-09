/*
 * NobleNext — gameobject mover handler.
 */

#include "noble_next_gobmover_handler.h"

#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayPhaseMgr.h"
#include "World.h"

#ifdef ELUNA
#include "ElunaTemplate.h"
#include "LuaEngine.h"
extern "C"
{
#include "lua.h"
}
#endif

namespace RoleplayCore::NobleNext
{
    namespace
    {
        ObjectGuid::LowType FindNearestSpawnIdInDb(Player* player, float range)
        {
            if (!player || range <= 0.f)
                return 0;

            WorldDatabasePreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_GAMEOBJECT_NEAREST);
            stmt->setFloat(0, player->GetPositionX());
            stmt->setFloat(1, player->GetPositionY());
            stmt->setFloat(2, player->GetPositionZ());
            stmt->setUInt32(3, player->GetMapId());
            stmt->setFloat(4, player->GetPositionX());
            stmt->setFloat(5, player->GetPositionY());
            stmt->setFloat(6, player->GetPositionZ());
            stmt->setFloat(7, range * range);

            PreparedQueryResult result = WorldDatabase.Query(stmt);
            if (!result)
                return 0;

            do
            {
                Field* fields = result->Fetch();
                ObjectGuid::LowType const spawnId = fields[0].GetUInt64();
                if (!spawnId || !sObjectMgr->GetGameObjectData(spawnId))
                    continue;

                RoleplayCommandPhaseGuard::SpawnContext const ctx = RoleplayCommandPhaseGuard::Resolve(
                    player, RoleplayPhaseSpawnType::GameObject, spawnId);
                if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(ctx, false))
                    continue;

                return spawnId;
            } while (result->NextRow());

            return 0;
        }

        GameObject* LoadLiveGameObject(Player* player, ObjectGuid::LowType spawnId)
        {
            if (!player || !spawnId)
                return nullptr;

            Map* map = player->GetMap();
            GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnId);
            if (!map || !data || data->mapId != player->GetMapId())
                return nullptr;

            map->LoadGrid(data->spawnPoint.GetPositionX(), data->spawnPoint.GetPositionY());
            return map->GetGameObjectBySpawnId(spawnId);
        }
    }

    GobMoverHandler& GobMoverHandler::Instance()
    {
        static GobMoverHandler instance;
        return instance;
    }

    GameObject* GobMoverHandler::FindNearestGameObject(Player* player, float range) const
    {
        if (!player)
            return nullptr;

        // IgnorePhases bypasses only native PhaseShift; logical RP context stays on the searcher.
        return player->FindNearestGameObjectWithOptions(range, { .IgnorePhases = true });
    }

    bool GobMoverHandler::NotifyClientTarget(Player* player, ObjectGuid::LowType spawnId, std::string const& name,
        uint64 logicalPhaseId)
    {
#ifdef ELUNA
        Eluna* eluna = sWorld->GetEluna();
        if (!eluna)
            return false;

        // Prefer global (set by NobleNext.lua); fall back to package.loaded.NobleNext.
        lua_getglobal(eluna->L, "NobleNext");
        if (!lua_istable(eluna->L, -1))
        {
            lua_pop(eluna->L, 1);
            lua_getglobal(eluna->L, "package");
            if (!lua_istable(eluna->L, -1))
            {
                lua_pop(eluna->L, 1);
                return false;
            }
            lua_getfield(eluna->L, -1, "loaded");
            lua_remove(eluna->L, -2);
            if (!lua_istable(eluna->L, -1))
            {
                lua_pop(eluna->L, 1);
                return false;
            }
            lua_getfield(eluna->L, -1, "NobleNext");
            lua_remove(eluna->L, -2);
            if (!lua_istable(eluna->L, -1))
            {
                lua_pop(eluna->L, 1);
                return false;
            }
        }

        lua_getfield(eluna->L, -1, "GobMoverSetTarget");
        lua_remove(eluna->L, -2);
        if (!lua_isfunction(eluna->L, -1))
        {
            lua_pop(eluna->L, 1);
            return false;
        }

        eluna->Push(player);
        eluna->Push(static_cast<unsigned long long>(spawnId));
        eluna->Push(name);
        eluna->Push(static_cast<unsigned long long>(logicalPhaseId));
        return eluna->ExecuteCall(4, 0);
#else
        (void)player;
        (void)spawnId;
        (void)name;
        (void)logicalPhaseId;
        return false;
#endif
    }

    bool GobMoverHandler::SelectNearestTarget(Player* player, uint64* targetPhaseId,
        ObjectGuid::LowType* selectedSpawnId, float range) const
    {
        if (!player)
            return false;

        GameObject* go = nullptr;

        // Prefer last .gob target / near selection when it is still in range.
        if (ObjectGuid::LowType const lastSpawnId = player->GetLastTargetedGO2())
        {
            if (GameObject* lastGo = LoadLiveGameObject(player, lastSpawnId))
            {
                if (player->GetDistance(lastGo) <= range && player->CanShareRoleplayContext(lastGo))
                    go = lastGo;
            }
        }

        if (!go)
            go = FindNearestGameObject(player, range);

        if (!go)
        {
            // Live grid can miss housing/unloaded cells; .gob near uses DB — mirror that, then load the grid.
            ObjectGuid::LowType const spawnId = FindNearestSpawnIdInDb(player, range);
            if (!spawnId)
                return false;

            go = LoadLiveGameObject(player, spawnId);
            if (!go)
                return false;
        }

        std::string name;
        if (GameObjectTemplate const* info = go->GetGOInfo())
            name = info->name;

        ObjectGuid::LowType const spawnId = go->GetSpawnId();
        uint64 const phaseId = sRoleplayPhaseMgr.GetSpawnPhaseId(RoleplayPhaseSpawnType::GameObject, spawnId, player->GetMapId());
        if (targetPhaseId)
            *targetPhaseId = phaseId;
        if (selectedSpawnId)
            *selectedSpawnId = spawnId;

        // Selection succeeds even if AIO notify fails (missing global / client addon).
        NotifyClientTarget(player, spawnId, name, phaseId);
        return true;
    }

    bool GobMoverHandler::SelectBySpawnId(Player* player, ObjectGuid::LowType spawnId, bool allowCrossPhase,
        uint64* targetPhaseId) const
    {
        if (!player || !spawnId)
            return false;

        GameObject* go = LoadLiveGameObject(player, spawnId);
        if (!go)
            return false;

        uint64 const phaseId = sRoleplayPhaseMgr.GetSpawnPhaseId(RoleplayPhaseSpawnType::GameObject, spawnId, player->GetMapId());
        if (!player->CanShareRoleplayContext(go) && !allowCrossPhase)
            return false;

        std::string name;
        if (GameObjectTemplate const* info = go->GetGOInfo())
            name = info->name;

        if (targetPhaseId)
            *targetPhaseId = phaseId;

        NotifyClientTarget(player, go->GetSpawnId(), name, phaseId);
        return true;
    }
}
