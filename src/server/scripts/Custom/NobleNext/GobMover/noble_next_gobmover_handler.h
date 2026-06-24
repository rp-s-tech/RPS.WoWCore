/*
 * NobleNext — gameobject mover handler (.movego → AIO target).
 */

#pragma once

#include "Define.h"
#include <string>

class GameObject;
class Player;

namespace RoleplayCore::NobleNext
{
    class GobMoverHandler
    {
    public:
        static GobMoverHandler& Instance();

        static constexpr float DefaultSearchRadius = 20.0f;

        GameObject* FindNearestGameObject(Player* player, float range = DefaultSearchRadius) const;
        bool SelectNearestTarget(Player* player, float range = DefaultSearchRadius) const;

    private:
        static bool NotifyClientTarget(Player* player, uint32 spawnId, std::string const& name);
    };
}
