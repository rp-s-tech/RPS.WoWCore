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
        bool SelectNearestTarget(Player* player, uint64* targetPhaseId = nullptr,
            ObjectGuid::LowType* selectedSpawnId = nullptr, float range = DefaultSearchRadius) const;
        /// Find live GO by spawnId on the player's map. Cross-logical-phase selection
        /// is allowed only after the command layer has verified explicit staff bypass.
        bool SelectBySpawnId(Player* player, ObjectGuid::LowType spawnId, bool allowCrossPhase,
            uint64* targetPhaseId = nullptr) const;

    private:
        static bool NotifyClientTarget(Player* player, ObjectGuid::LowType spawnId, std::string const& name, uint64 logicalPhaseId);
    };
}
