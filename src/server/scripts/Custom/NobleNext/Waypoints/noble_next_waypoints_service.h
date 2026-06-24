/*
 * NobleNext — dynamic waypoints service.
 */

#pragma once

#include "Define.h"
#include <unordered_map>
#include <vector>

class Creature;
class Player;

namespace RoleplayCore::NobleNext
{
    class WaypointsService
    {
    public:
        static WaypointsService& Instance();

        void ClearRoute(Creature* creature);
        void AddMovePoint(Creature* creature, float x, float y, float z, bool walk);
        void AddWaitPoint(Creature* creature, float seconds, uint32 emoteId);
        void StartRoute(Creature* creature);
        void StopRoute(Creature* creature);
        void SetStandState(Creature* creature, uint32 standState);
        size_t PointCount(Creature* creature) const;

    private:
        struct Point
        {
            uint8 Type = 1; // 1 move run, 3 walk, 2 wait
            float X = 0, Y = 0, Z = 0;
            uint32 WaitMs = 0;
            uint32 EmoteId = 0;
        };

        struct ActiveRoute
        {
            std::vector<Point> Points;
            size_t Index = 0;
            uint32 CreatureSpawnId = 0;
            uint32 MapId = 0;
        };

        std::unordered_map<uint32, std::vector<Point>> _routes;
        std::unordered_map<uint32, ActiveRoute> _active;

        std::vector<Point>& RouteFor(Creature* creature);
        void Advance(uint32 spawnId);
    };
}
