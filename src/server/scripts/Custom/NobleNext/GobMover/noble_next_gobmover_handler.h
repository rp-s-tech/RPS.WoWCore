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
        GameObject* SelectNearestTarget(Player* player, bool preferLast) const;
        GameObject* SelectBySpawnId(Player* player, ObjectGuid::LowType spawnId, bool allowCrossPhase) const;
        static bool NotifyClientTarget(Player* player, GameObject* go, uint64 logicalPhaseId, uint32 requestId);
    };
}
