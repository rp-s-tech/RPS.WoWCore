/*
 * NobleNext — gameobject mover handler (.movego → AIO target).
 */

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class GameObject;
class Player;

namespace RoleplayCore::NobleNext
{
    class GobMoverHandler
    {
    public:
        static GobMoverHandler& Instance();

        static constexpr float DefaultSearchRadius = 50.0f;

        GameObject* FindNearestGameObject(Player* player, float range = DefaultSearchRadius) const;
        bool SelectNearestTarget(Player* player, float range = DefaultSearchRadius) const;
        /// Find live GO by spawnId on the player's map (spawn store; no phase filter).
        bool SelectBySpawnId(Player* player, ObjectGuid::LowType spawnId) const;

    private:
        static bool NotifyClientTarget(Player* player, ObjectGuid::LowType spawnId, std::string const& name);
    };
}
