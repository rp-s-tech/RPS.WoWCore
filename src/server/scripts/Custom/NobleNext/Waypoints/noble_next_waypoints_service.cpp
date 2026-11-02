#include "noble_next_waypoints_service.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DB2Stores.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "ScriptActions.h"
#include "SharedDefines.h"
#include "WaypointManager.h"
#include "WaypointMovementGenerator.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace RoleplayCore::NobleNext
{
// Low EVENT_* IDs have native behavior; EVENT_CHARGE_PREPATH skips ordinary spline launch.
constexpr uint32 RouteMovementIdBase = 0x4E4E0000;

struct WaypointsService::MoveResult : Scripting::v2::ActionBase
{
    Scripting::v2::ActionResultValueHolder<MovementStopReason> Value{*this, MovementStopReason::Interrupted};
};

void WaypointsService::BeginMove(Creature* c, Point const& p, Active& state)
{
    c->GetMotionMaster()->MoveIdle(); // Also reclaim the default slot after native respawn/combat resets.
    state.Movement = std::make_shared<MoveResult>();
    auto value = std::shared_ptr<Scripting::v2::ActionResultValueHolder<MovementStopReason>>(state.Movement, &state.Movement->Value);
    c->SetEmoteState(EMOTE_ONESHOT_NONE);
    c->SetWalk(p.Type == 3);
    c->GetMotionMaster()->MovePoint(RouteMovementIdBase + uint32(state.Index), p.X, p.Y, p.Z, true, {}, {},
        p.Type == 3 ? MovementWalkRunSpeedSelectionMode::ForceWalk : MovementWalkRunSpeedSelectionMode::ForceRun,
        {}, {}, Scripting::v2::ActionResultSetter<MovementStopReason>(std::move(value)));
}

WaypointsService& WaypointsService::Instance() { static WaypointsService value; return value; }
WaypointsService::Key WaypointsService::KeyFor(Creature const* c) { return {c->GetMapId(), c->GetInstanceId(), c->GetGUID()}; }

bool WaypointsService::IsLocked(Creature const* c) const
{
    std::lock_guard lock(_mutex);
    return c && _locks.contains(KeyFor(c));
}
bool WaypointsService::LockForBake(Creature* c, Snapshot& snapshot, bool requireDraft)
{
    std::lock_guard lock(_mutex);
    if (!c || (requireDraft && !_routes.contains(KeyFor(c))) || !_locks.insert(KeyFor(c)).second) return false;
    auto it = _routes.find(KeyFor(c));
    if (it != _routes.end()) snapshot = it->second;
    c->GetMotionMaster()->Clear(); // Freeze the source position until its replacement is confirmed.
    c->GetMotionMaster()->MoveIdle();
    return true;
}
void WaypointsService::Unlock(Key const& key) { std::lock_guard lock(_mutex); _locks.erase(key); }
bool WaypointsService::ClearRoute(Creature* c)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c)) return false;
    StopRoute(c);
    _routes[KeyFor(c)].Points.clear(); // Retain the native binding while editing an empty draft.
    _routes[KeyFor(c)].Revision = ++_revision;
    return true;
}
bool WaypointsService::AddMovePoint(Creature* c, float x, float y, float z, bool walk)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    auto& points = _routes[KeyFor(c)].Points;
    if (points.size() >= 1024) return false;
    points.push_back(Point{uint8(walk ? 3 : 1), x, y, z});
    _routes[KeyFor(c)].Revision = ++_revision;
    return true;
}
bool WaypointsService::AddWaitPoint(Creature* c, float seconds, uint32 emoteId)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c) || !std::isfinite(seconds) || seconds < 0.5f || seconds > 4294967.0f
        || (emoteId && !sEmotesStore.LookupEntry(emoteId))) return false;
    auto& points = _routes[KeyFor(c)].Points;
    if (points.size() >= 1024) return false;
    Point point;
    point.Type = 2;
    point.WaitMs = uint32(double(seconds) * 1000.0);
    point.EmoteId = emoteId;
    points.push_back(point);
    _routes[KeyFor(c)].Revision = ++_revision;
    return true;
}
bool WaypointsService::StopRoute(Creature* c)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c)) return false;
    _active.erase(KeyFor(c));
    c->GetMotionMaster()->Clear();
    c->GetMotionMaster()->MoveIdle();
    return true;
}
bool WaypointsService::StartRoute(Creature* c)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c)) return false;
    auto it = _routes.find(KeyFor(c));
    if (it == _routes.end() || it->second.Points.empty()) return false;
    c->GetMotionMaster()->Clear();
    c->GetMotionMaster()->MoveIdle();
    _active[KeyFor(c)] = {};
    return true;
}
void WaypointsService::SetStandState(Creature* c, uint32 state)
{
    std::lock_guard lock(_mutex);
    if (c && !IsLocked(c) && state < MAX_UNIT_STAND_STATE) c->SetStandState(UnitStandStateType(state));
}
size_t WaypointsService::PointCount(Creature* c) const
{
    std::lock_guard lock(_mutex);
    if (!c) return 0;
    auto it = _routes.find(KeyFor(c));
    return it == _routes.end() ? 0 : it->second.Points.size();
}
WaypointsService::View WaypointsService::ReadRoute(Creature* c) const
{
    std::lock_guard lock(_mutex);
    View view;
    if (!c) return view;
    Key key = KeyFor(c);
    view.Locked = _locks.contains(key);
    auto route = _routes.find(key);
    if (route == _routes.end())
    {
        // Read the real movement path without installing a draft or changing the creature's AI/motion.
        MotionMaster* motion = c->GetMotionMaster();
        auto waypoint = [](MovementGenerator const* movement) { return movement->GetMovementGeneratorType() == WAYPOINT_MOTION_TYPE; };
        MovementGenerator* movement = motion->GetMovementGenerator(waypoint, MOTION_SLOT_ACTIVE);
        if (!movement) movement = motion->GetMovementGenerator(waypoint, MOTION_SLOT_DEFAULT);
        auto* native = static_cast<WaypointMovementGenerator<Creature>*>(movement);
        WaypointPath const* path = nullptr;
        bool loaded = native && !native->HasFlag(MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_FINALIZED);
        if (loaded)
            path = native->GetPath(); // Includes SmartAI and C++ paths with no creature_addon binding.
        if (!path) path = sWaypointMgr->GetPath(c->GetWaypointPathId());
        if (!path) return view;
        view.Native = true;
        view.Route.PathId = path->Id;
        view.Route.Points.reserve(path->Nodes.size());
        for (WaypointNode const& node : path->Nodes)
        {
            WaypointMoveType mode = node.MoveType.value_or(path->MoveType);
            uint8 type = mode == WaypointMoveType::Walk ? 3 : mode == WaypointMoveType::Land ? 4 : mode == WaypointMoveType::TakeOff ? 5 : 1;
            uint32 delay = uint32(std::clamp<int64>(node.Delay.value_or(Milliseconds(0)).count(), 0, std::numeric_limits<uint32>::max()));
            view.Route.Points.push_back(Point{type, node.X, node.Y, node.Z, delay});
        }
        if (loaded && path == native->GetPath())
        {
            if (native->GetCurrentNode() < path->Nodes.size()) view.Current = native->GetCurrentNode() + 1;
            view.Running = c->IsAlive() && !c->IsInCombat() && !c->IsCharmed()
                && !native->HasFlag(MOVEMENTGENERATOR_FLAG_PAUSED | MOVEMENTGENERATOR_FLAG_TIMED_PAUSED | MOVEMENTGENERATOR_FLAG_DEACTIVATED
                    | MOVEMENTGENERATOR_FLAG_INITIALIZATION_PENDING | MOVEMENTGENERATOR_FLAG_FINALIZED);
        }
        return view;
    }
    view.Route = route->second;
    if (auto active = _active.find(key); active != _active.end())
    {
        view.Current = active->second.Index + 1;
        if (active->second.WaitMs && !active->second.Movement && !view.Route.Points.empty())
            view.Current = (active->second.Index + view.Route.Points.size() - 1) % view.Route.Points.size() + 1;
        view.Running = !active->second.Blocked;
        view.Blocked = active->second.Blocked;
    }
    return view;
}
bool WaypointsService::RemovePoint(Creature* c, uint32 point, uint32 revision)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c)) return false;
    auto it = _routes.find(KeyFor(c));
    if (it == _routes.end() || !point || point > it->second.Points.size()
        || (revision && revision != it->second.Revision)) return false;
    StopRoute(c);
    it->second.Points.erase(it->second.Points.begin() + point - 1);
    it->second.Revision = ++_revision;
    return true;
}
bool WaypointsService::SwapPoints(Creature* c, uint32 first, uint32 second, uint32 revision)
{
    std::lock_guard lock(_mutex);
    if (!c || IsLocked(c)) return false;
    auto it = _routes.find(KeyFor(c));
    if (it == _routes.end() || !first || !second || first > it->second.Points.size() || second > it->second.Points.size()
        || (revision && revision != it->second.Revision)) return false;
    StopRoute(c);
    std::swap(it->second.Points[first - 1], it->second.Points[second - 1]);
    it->second.Revision = ++_revision;
    return true;
}
bool WaypointsService::AppendSave(WorldDatabaseTransaction const& transaction, uint64 spawnId, uint32 mapId,
    Snapshot const& snapshot, uint32 previousPathId)
{
    CreatureData const* data = sObjectMgr->GetCreatureData(spawnId);
    if (!data || data->mapId != mapId || snapshot.Points.size() > 1024
        || (snapshot.Points.empty() != (snapshot.PathId == 0))) return false;
    if (snapshot.PathId)
    {
        // Copy-on-write: an existing native path may also belong to other creatures.
        transaction->PAppend("INSERT INTO waypoint_path (PathId, MoveType, Flags, Comment) VALUES ({}, 1, 0, 'NobleNext route')", snapshot.PathId);
        float x = data->spawnPoint.GetPositionX(), y = data->spawnPoint.GetPositionY(), z = data->spawnPoint.GetPositionZ();
        for (size_t i = 0; i < snapshot.Points.size(); ++i)
        {
            Point const& p = snapshot.Points[i];
            if (p.Type != 2) { x = p.X; y = p.Y; z = p.Z; }
            transaction->PAppend("INSERT INTO waypoint_path_node (PathId, NodeId, PositionX, PositionY, PositionZ, Delay) VALUES ({}, {}, {}, {}, {}, {})",
                snapshot.PathId, i + 1, x, y, z, p.WaitMs);
            transaction->PAppend("INSERT INTO rps_waypoint_action (PathId, NodeId, kind, emote) VALUES ({}, {}, {}, {})",
                snapshot.PathId, i + 1, p.Type, p.EmoteId);
        }
    }
    // A new per-spawn addon shadows template properties: copy them once, and only update PathId on existing addons.
    transaction->PAppend("INSERT INTO creature_addon (guid, PathId, mount, MountCreatureID, StandState, AnimTier, VisFlags, SheathState, PvPFlags, emote, aiAnimKit, movementAnimKit, meleeAnimKit, visibilityDistanceType, auras) SELECT c.guid, {}, COALESCE(t.mount,0), COALESCE(t.MountCreatureID,0), COALESCE(t.StandState,0), COALESCE(t.AnimTier,0), COALESCE(t.VisFlags,0), COALESCE(t.SheathState,1), COALESCE(t.PvPFlags,0), COALESCE(t.emote,0), COALESCE(t.aiAnimKit,0), COALESCE(t.movementAnimKit,0), COALESCE(t.meleeAnimKit,0), COALESCE(t.visibilityDistanceType,0), t.auras FROM creature c LEFT JOIN creature_template_addon t ON t.entry = c.id WHERE c.guid = {} AND c.map = {} AND c.id = {} ON DUPLICATE KEY UPDATE PathId = IF(creature_addon.PathId = {}, {}, creature_addon.PathId)",
        snapshot.PathId, spawnId, mapId, data->id, previousPathId, snapshot.PathId);
    transaction->PAppend("UPDATE creature c JOIN creature_addon a ON a.guid = c.guid SET c.MovementType = {} WHERE c.guid = {} AND c.map = {} AND c.id = {} AND a.PathId = {}",
        snapshot.PathId ? 2 : 0, spawnId, mapId, data->id, snapshot.PathId);
    for (uint32 path : {previousPathId, snapshot.PathId})
        if (path)
        {
            // Only collect paths owned by this module and no longer bound to any creature or template.
            transaction->PAppend("DELETE n FROM waypoint_path_node n JOIN waypoint_path p ON p.PathId = n.PathId WHERE p.PathId = {} AND p.Comment = 'NobleNext route' AND NOT EXISTS (SELECT 1 FROM creature_addon WHERE PathId = p.PathId) AND NOT EXISTS (SELECT 1 FROM creature_template_addon WHERE PathId = p.PathId)", path);
            transaction->PAppend("DELETE a FROM rps_waypoint_action a JOIN waypoint_path p ON p.PathId = a.PathId WHERE p.PathId = {} AND p.Comment = 'NobleNext route' AND NOT EXISTS (SELECT 1 FROM creature_addon WHERE PathId = p.PathId) AND NOT EXISTS (SELECT 1 FROM creature_template_addon WHERE PathId = p.PathId)", path);
            transaction->PAppend("DELETE p FROM waypoint_path p WHERE p.PathId = {} AND p.Comment = 'NobleNext route' AND NOT EXISTS (SELECT 1 FROM creature_addon WHERE PathId = p.PathId) AND NOT EXISTS (SELECT 1 FROM creature_template_addon WHERE PathId = p.PathId)", path);
        }
    return true;
}
void WaypointsService::InstallSaved(Creature* c, Snapshot const& snapshot)
{
    std::lock_guard lock(_mutex);
    _saved[c->GetSpawnId()] = {c->GetMapId(), snapshot};
    _routes[KeyFor(c)] = snapshot;
    _routes[KeyFor(c)].Revision = ++_revision;
    _active.erase(KeyFor(c));
    _locks.erase(KeyFor(c));
    c->LoadPath(snapshot.PathId);
    c->SetDefaultMovementType(snapshot.PathId ? WAYPOINT_MOTION_TYPE : IDLE_MOTION_TYPE);
    c->GetMotionMaster()->Clear();
    c->GetMotionMaster()->MoveIdle(); // The action controller drives native point movement, including per-node modes/emotes.
    if (!snapshot.Points.empty()) _active[KeyFor(c)] = {};
}
void WaypointsService::RememberSaved(uint64 spawnId, uint32 mapId, Snapshot const& snapshot)
{
    std::lock_guard lock(_mutex);
    _saved[spawnId] = {mapId, snapshot};
}
void WaypointsService::LoadSaved()
{
    std::lock_guard lock(_mutex);
    _saved.clear();
    QueryResult rows = WorldDatabase.Query("SELECT c.guid, c.map, c.id, p.PathId, n.NodeId, n.PositionX, n.PositionY, n.PositionZ, n.Delay, a.kind, a.emote FROM creature c JOIN creature_addon ca ON ca.guid = c.guid JOIN waypoint_path p ON p.PathId = ca.PathId LEFT JOIN waypoint_path_node n ON n.PathId = p.PathId LEFT JOIN rps_waypoint_action a ON a.PathId = n.PathId AND a.NodeId = n.NodeId WHERE p.Comment = 'NobleNext route' ORDER BY c.guid, n.NodeId");
    if (!rows) return;
    std::set<uint64> invalid;
    do
    {
        Field* f = rows->Fetch();
        uint64 spawn = f[0].GetUInt64();
        CreatureData const* data = sObjectMgr->GetCreatureData(spawn);
        if (!data) continue;
        auto& saved = _saved[spawn];
        saved.MapId = data->mapId;
        saved.Route.PathId = f[3].GetUInt32();
        if (f[4].IsNull() || f[9].IsNull()) { invalid.insert(spawn); continue; }
        Point p{f[9].GetUInt8(), f[5].GetFloat(), f[6].GetFloat(), f[7].GetFloat(), f[8].GetUInt32(), f[10].GetUInt32()};
        if (data->mapId != f[1].GetUInt32() || data->id != f[2].GetUInt32() || f[4].GetUInt32() != saved.Route.Points.size() + 1
            || (p.Type != 1 && p.Type != 2 && p.Type != 3)
            || !std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z)
            || (p.Type == 2 && p.WaitMs < 500) || (p.EmoteId && !sEmotesStore.LookupEntry(p.EmoteId)))
        { invalid.insert(spawn); continue; }
        if (saved.Route.Points.size() < 1024) saved.Route.Points.push_back(p);
        else invalid.insert(spawn);
    } while (rows->NextRow());
    for (uint64 spawn : invalid)
    {
        _saved[spawn].Route.Points.clear(); // Suppress vanilla fallback if extension data is incomplete.
        TC_LOG_ERROR("scripts", "NobleNext route {} for spawn {} has invalid or missing actions; movement disabled.", _saved[spawn].Route.PathId, spawn);
    }
}
void WaypointsService::UpdateMap(Map* map, uint32 diff)
{
    std::lock_guard lock(_mutex);
    // All runtime keys include map, instance and GUID; temporary creatures never share spawnId=0 state.
    auto onMap = [map](Key const& k) { return k.MapId == map->GetId() && k.InstanceId == map->GetInstanceId(); };
    uint32& timer = _scanTimers[map->GetId()];
    if (timer <= diff)
    {
        timer = 1000;
        for (auto it = _routes.begin(); it != _routes.end();)
        {
            Creature* c = onMap(it->first) ? map->GetCreature(it->first.Guid) : nullptr;
            bool changed = c && it->second.PathId && c->GetWaypointPathId() != it->second.PathId;
            if (onMap(it->first) && (!c || changed) && !_locks.contains(it->first))
            {
                if (changed) _saved.erase(c->GetSpawnId()); // An explicit native .wp load/unload takes precedence.
                _active.erase(it->first); it = _routes.erase(it);
            }
            else ++it;
        }
        for (auto const& [spawnId, saved] : _saved)
            if (saved.MapId == map->GetId())
                if (Creature* c = map->GetCreatureBySpawnId(spawnId); c && !_routes.contains(KeyFor(c)))
                    if (CreatureData const* data = c->GetCreatureData(); data && data->movementType == WAYPOINT_MOTION_TYPE)
                        // A newly inserted addon is not in ObjectMgr's cache until reload; the creature may still inherit a template path.
                        if (c->GetWaypointPathId() == saved.Route.PathId || !sObjectMgr->GetCreatureAddon(spawnId))
                            InstallSaved(c, saved.Route);
    }
    else timer -= diff;
    for (auto it = _active.begin(); it != _active.end();)
    {
        Key const& key = it->first;
        if (!onMap(key)) { ++it; continue; }
        Creature* c = map->GetCreature(key.Guid);
        auto route = _routes.find(key);
        if (!c || route == _routes.end() || route->second.Points.empty()) { it = _active.erase(it); continue; }
        Active& state = it->second;
        ++it;
        if (!c->IsAlive() || c->IsInCombat() || c->IsCharmed() || _locks.contains(key) || state.Blocked) continue;
        if (state.WaitMs) { state.WaitMs = state.WaitMs > diff ? state.WaitMs - diff : 0; continue; }
        auto const& points = route->second.Points;
        Point const& p = points[state.Index % points.size()];
        if (state.Movement)
        {
            if (!state.Movement->IsReady()) continue;
            if (state.Movement->Value.Result != MovementStopReason::Finished)
            {
                BeginMove(c, p, state);
                state.WaitMs = 1000;
                continue;
            }
            // Native pathfinding normalizes Z for this creature. The recorded player's Z is not its arrival height.
            float z = p.Z;
            c->UpdateAllowedPositionZ(p.X, p.Y, z);
            if (c->GetExactDist(p.X, p.Y, z) > 1.0f)
            {
                state.Blocked = true;
                TC_LOG_WARN("scripts", "NobleNext route {} stopped at action {}: destination ({}, {}, {}) is unreachable from ({}, {}, {}).",
                    c->GetGUID().ToString(), state.Index + 1, p.X, p.Y, z, c->GetPositionX(), c->GetPositionY(), c->GetPositionZ());
                continue;
            }
            state.Movement.reset();
            state.WaitMs = p.WaitMs;
            state.Index = (state.Index + 1) % points.size();
            continue;
        }
        if (p.Type == 2)
        {
            c->SetEmoteState(Emote(p.EmoteId));
            state.WaitMs = p.WaitMs;
            state.Index = (state.Index + 1) % points.size();
        }
        else
        {
            BeginMove(c, p, state);
        }
    }
}
}
