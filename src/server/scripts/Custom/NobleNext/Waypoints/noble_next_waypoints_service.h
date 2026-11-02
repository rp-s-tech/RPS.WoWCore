/* NobleNext temporary routes and their explicitly saved snapshots. */
#pragma once
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>
class Creature;
class Map;

namespace RoleplayCore::NobleNext
{
class WaypointsService
{
public:
    struct Point
    {
        uint8 Type = 1; // run=1, wait/emote=2, walk=3; native view only: land=4, takeoff=5
        float X = 0, Y = 0, Z = 0;
        uint32 WaitMs = 0, EmoteId = 0;
    };
    struct Snapshot { std::vector<Point> Points; uint32 Revision = 0, PathId = 0; };
    struct View
    {
        Snapshot Route;
        size_t Current = 0;
        bool Running = false, Blocked = false, Locked = false, Native = false;
    };
    struct Key
    {
        uint32 MapId, InstanceId;
        ObjectGuid Guid;
        bool operator<(Key const& other) const
        { return std::tie(MapId, InstanceId, Guid) < std::tie(other.MapId, other.InstanceId, other.Guid); }
    };
    static WaypointsService& Instance();
    static Key KeyFor(Creature const* creature);
    bool ClearRoute(Creature* creature);
    bool AddMovePoint(Creature* creature, float x, float y, float z, bool walk);
    bool AddWaitPoint(Creature* creature, float seconds, uint32 emoteId);
    bool StartRoute(Creature* creature);
    bool StopRoute(Creature* creature);
    void SetStandState(Creature* creature, uint32 standState);
    size_t PointCount(Creature* creature) const;
    View ReadRoute(Creature* creature) const;
    bool RemovePoint(Creature* creature, uint32 point, uint32 revision = 0);
    bool SwapPoints(Creature* creature, uint32 first, uint32 second, uint32 revision = 0);
    bool LockForBake(Creature* creature, Snapshot& snapshot, bool requireDraft = false);
    bool IsLocked(Creature const* creature) const;
    void Unlock(Key const& key);
    void InstallSaved(Creature* creature, Snapshot const& snapshot);
    void RememberSaved(uint64 spawnId, uint32 mapId, Snapshot const& snapshot);
    static bool AppendSave(WorldDatabaseTransaction const& transaction, uint64 spawnId, uint32 mapId,
        Snapshot const& snapshot, uint32 previousPathId);
    void LoadSaved();
    void UpdateMap(Map* map, uint32 diff);
private:
    struct MoveResult;
    struct Active
    {
        size_t Index = 0;
        uint32 WaitMs = 0;
        bool Blocked = false;
        std::shared_ptr<MoveResult> Movement;
    };
    static void BeginMove(Creature* creature, Point const& point, Active& state);
    uint32 _revision = 0;
    struct Saved { uint32 MapId; Snapshot Route; };
    mutable std::recursive_mutex _mutex;
    std::map<Key, Snapshot> _routes;
    std::map<Key, Active> _active;
    std::set<Key> _locks;
    std::map<uint64, Saved> _saved;
    std::map<uint32, uint32> _scanTimers;
};
}
