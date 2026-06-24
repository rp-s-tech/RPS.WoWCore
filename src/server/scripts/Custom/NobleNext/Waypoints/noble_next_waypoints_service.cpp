/*
 * NobleNext — dynamic waypoints service.
 */

#include "noble_next_waypoints_service.h"

#include "Creature.h"
#include "Duration.h"
#include "Map.h"
#include "MapManager.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"

namespace RoleplayCore::NobleNext
{
    WaypointsService& WaypointsService::Instance()
    {
        static WaypointsService instance;
        return instance;
    }

    std::vector<WaypointsService::Point>& WaypointsService::RouteFor(Creature* creature)
    {
        return _routes[creature->GetSpawnId()];
    }

    void WaypointsService::ClearRoute(Creature* creature)
    {
        if (!creature) return;
        _routes.erase(creature->GetSpawnId());
        StopRoute(creature);
    }

    void WaypointsService::AddMovePoint(Creature* creature, float x, float y, float z, bool walk)
    {
        if (!creature) return;
        Point p;
        p.Type = walk ? 3 : 1;
        p.X = x; p.Y = y; p.Z = z;
        RouteFor(creature).push_back(p);
    }

    void WaypointsService::AddWaitPoint(Creature* creature, float seconds, uint32 emoteId)
    {
        if (!creature) return;
        Point p;
        p.Type = 2;
        p.WaitMs = uint32(seconds * 1000.f);
        p.EmoteId = emoteId;
        RouteFor(creature).push_back(p);
    }

    void WaypointsService::SetStandState(Creature* creature, uint32 standState)
    {
        if (!creature)
            return;

        if (standState >= MAX_UNIT_STAND_STATE)
            standState = UNIT_STAND_STATE_STAND;

        creature->SetStandState(static_cast<UnitStandStateType>(standState));
    }

    size_t WaypointsService::PointCount(Creature* creature) const
    {
        if (!creature)
            return 0;
        auto it = _routes.find(creature->GetSpawnId());
        return it != _routes.end() ? it->second.size() : 0;
    }

    void WaypointsService::StopRoute(Creature* creature)
    {
        if (!creature) return;
        _active.erase(creature->GetSpawnId());
        creature->GetMotionMaster()->Clear();
    }

    void WaypointsService::Advance(uint32 spawnId)
    {
        auto it = _active.find(spawnId);
        if (it == _active.end())
            return;

        ActiveRoute& route = it->second;
        if (route.Points.empty())
        {
            _active.erase(it);
            return;
        }

        if (route.Index >= route.Points.size())
            route.Index = 0;

        Point const& pt = route.Points[route.Index];
        Map* map = sMapMgr->FindMap(route.MapId, 0);
        if (!map)
        {
            _active.erase(it);
            return;
        }

        Creature* creature = map->GetCreatureBySpawnId(spawnId);
        if (!creature)
        {
            _active.erase(it);
            return;
        }

        if (pt.Type == 2)
        {
            creature->SetEmoteState(static_cast<Emote>(pt.EmoteId));
            creature->m_Events.AddEventAtOffset([this, spawnId]()
            {
                Advance(spawnId);
            }, Milliseconds(pt.WaitMs));
            route.Index++;
            return;
        }

        creature->SetEmoteState(EMOTE_ONESHOT_NONE);
        creature->SetWalk(pt.Type == 3);
        creature->GetMotionMaster()->MovePoint(1000 + uint32(route.Index), pt.X, pt.Y, pt.Z);

        float dist = creature->GetExactDist(pt.X, pt.Y, pt.Z);
        float speed = creature->GetSpeed(MOVE_RUN);
        if (speed < 0.1f) speed = 7.f;
        uint32 delayMs = uint32((dist / speed) * 1000.f) + 100;

        route.Index++;
        creature->m_Events.AddEventAtOffset([this, spawnId]()
        {
            Advance(spawnId);
        }, Milliseconds(delayMs));
    }

    void WaypointsService::StartRoute(Creature* creature)
    {
        if (!creature) return;
        auto rit = _routes.find(creature->GetSpawnId());
        if (rit == _routes.end() || rit->second.empty())
            return;

        ActiveRoute ar;
        ar.Points = rit->second;
        ar.Index = 0;
        ar.CreatureSpawnId = creature->GetSpawnId();
        ar.MapId = creature->GetMapId();
        _active[creature->GetSpawnId()] = std::move(ar);
        Advance(creature->GetSpawnId());
    }
}
