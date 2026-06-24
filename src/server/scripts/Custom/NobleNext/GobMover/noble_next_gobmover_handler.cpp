/*
 * NobleNext — gameobject mover handler.
 */

#include "noble_next_gobmover_handler.h"

#include "GameObject.h"
#include "Player.h"
#include "World.h"

#include <list>

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
    GobMoverHandler& GobMoverHandler::Instance()
    {
        static GobMoverHandler instance;
        return instance;
    }

    GameObject* GobMoverHandler::FindNearestGameObject(Player* player, float range) const
    {
        if (!player)
            return nullptr;

        std::list<GameObject*> goList;
        player->GetGameObjectListWithEntryInGrid(goList, 0, range);

        GameObject* nearest = nullptr;
        float nearestDist = range;

        for (GameObject* go : goList)
        {
            if (!go || !player->InSamePhase(go))
                continue;

            float dist = player->GetExactDist(go);
            if (dist <= nearestDist)
            {
                nearestDist = dist;
                nearest = go;
            }
        }

        return nearest;
    }

    bool GobMoverHandler::NotifyClientTarget(Player* player, uint32 spawnId, std::string const& name)
    {
#ifdef ELUNA
        Eluna* eluna = sWorld->GetEluna();
        if (!eluna)
            return false;

        lua_getglobal(eluna->L, "NobleNext");
        if (!lua_istable(eluna->L, -1))
        {
            lua_pop(eluna->L, 1);
            return false;
        }

        lua_getfield(eluna->L, -1, "GobMoverSetTarget");
        lua_remove(eluna->L, -2);
        if (!lua_isfunction(eluna->L, -1))
        {
            lua_pop(eluna->L, 1);
            return false;
        }

        eluna->Push(player);
        eluna->Push(spawnId);
        eluna->Push(name);
        return eluna->ExecuteCall(3, 0);
#else
        (void)player;
        (void)spawnId;
        (void)name;
        return false;
#endif
    }

    bool GobMoverHandler::SelectNearestTarget(Player* player, float range) const
    {
        GameObject* go = FindNearestGameObject(player, range);
        if (!go)
            return false;

        std::string name;
        if (GameObjectTemplate const* info = go->GetGOInfo())
            name = info->name;

        return NotifyClientTarget(player, go->GetSpawnId(), name);
    }
}
