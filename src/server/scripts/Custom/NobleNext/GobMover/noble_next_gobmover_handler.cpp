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

    bool GobMoverHandler::NotifyClientTarget(Player* player, GameObject* go, uint64 logicalPhaseId, uint32 requestId)
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
        eluna->Push(std::to_string(go->GetSpawnId()));
        eluna->Push(go->GetGOInfo()->name);
        eluna->Push(std::to_string(logicalPhaseId));
        eluna->Push(go->GetEntry());
        eluna->Push(requestId);
        return eluna->ExecuteCall(6, 0);
#else
        (void)player;
        (void)go;
        (void)requestId;
        (void)logicalPhaseId;
        return false;
#endif
    }

    GameObject* GobMoverHandler::SelectNearestTarget(Player* player, bool preferLast) const
    {
        if (!player)
            return nullptr;

        GameObject* go = nullptr;

        // Prefer last .gob target / near selection when it is still in range.
        if (ObjectGuid::LowType const lastSpawnId = preferLast ? player->GetLastTargetedGO2() : 0)
        {
            if (GameObject* lastGo = LoadLiveGameObject(player, lastSpawnId))
            {
                if (player->GetDistance(lastGo) <= DefaultSearchRadius && player->CanShareRoleplayContext(lastGo))
                    go = lastGo;
            }
        }

        if (!go)
            go = FindNearestGameObject(player, DefaultSearchRadius);

        if (!go)
        {
            // Live grid can miss housing/unloaded cells; .gob near uses DB — mirror that, then load the grid.
            ObjectGuid::LowType const spawnId = FindNearestSpawnIdInDb(player, DefaultSearchRadius);
            if (!spawnId)
                return nullptr;

            go = LoadLiveGameObject(player, spawnId);
            if (!go)
                return nullptr;
        }

        return go;
    }

    GameObject* GobMoverHandler::SelectBySpawnId(Player* player, ObjectGuid::LowType spawnId, bool allowCrossPhase) const
    {
        GameObject* go = LoadLiveGameObject(player, spawnId);
        return go && (player->CanShareRoleplayContext(go) || allowCrossPhase) ? go : nullptr;
    }
}
