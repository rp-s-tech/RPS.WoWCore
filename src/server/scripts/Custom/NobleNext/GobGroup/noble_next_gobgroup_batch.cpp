/*
 * NobleNext — same-tick runtime batch for GobGroup / GobBlueprint.
 */

#include "noble_next_gobgroup_batch.h"

#include "GameObject.h"
#include "GameObjectData.h"
#include "GridDefines.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "StringFormat.h"
#include "Timer.h"

namespace RoleplayCore::NobleNext
{
namespace GobGroupBatch
{
    void ApplyPlanToCache(GroupTransformPlan const& plan, std::string& errorOut)
    {
        for (GroupTransformRow const& row : plan.Rows)
        {
            GameObjectData* data = const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(row.SpawnId));
            if (!data)
            {
                if (errorOut.empty())
                    errorOut = Trinity::StringFormat("Spawn {} disappeared during cache apply", row.SpawnId);
                continue;
            }

            bool const gridChanged = row.OldMapId != row.NewMapId
                || Trinity::ComputeGridCoord(row.OldWorld.GetPositionX(), row.OldWorld.GetPositionY())
                    != Trinity::ComputeGridCoord(row.NewWorld.GetPositionX(), row.NewWorld.GetPositionY());

            if (gridChanged)
                sObjectMgr->RemoveGameobjectFromGrid(data);

            data->mapId = row.NewMapId;
            data->spawnPoint.Relocate(row.NewWorld);
            data->rotation = row.NewRotation;

            if (gridChanged)
                sObjectMgr->AddGameobjectToGrid(data);
        }
    }

    void HideLive(Map* map, GroupTransformPlan const& plan)
    {
        if (!map)
            return;

        auto findLive = [&](ObjectGuid::LowType spawnId) -> GameObject*
        {
            auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(spawnId);
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
                if (itr->second)
                    return itr->second;
            return nullptr;
        };

        for (GroupTransformRow const& row : plan.Rows)
        {
            if (GameObject* go = findLive(row.SpawnId))
            {
                go->DestroyForNearbyPlayers();
                go->Delete();
            }
        }
    }

    void RevealAll(Map* map, GroupTransformPlan const& plan, std::string& errorOut)
    {
        if (!map)
            return;

        for (GroupTransformRow const& row : plan.Rows)
        {
            if (!map->IsGridLoaded(row.NewWorld))
                continue;

            if (!GameObject::CreateGameObjectFromDB(row.SpawnId, map, true))
            {
                if (errorOut.empty())
                    errorOut = Trinity::StringFormat("Failed to recreate spawn {} on map {}", row.SpawnId, row.NewMapId);
            }
        }
    }

    void RelocateExisting(Map* map, GroupTransformPlan const& plan, std::string& errorOut)
    {
        HideLive(map, plan);
        ApplyPlanToCache(plan, errorOut);
        RevealAll(map, plan, errorOut);
    }

    void SpawnNew(Map* map, std::vector<ObjectGuid::LowType> const& spawnIds, std::string& errorOut)
    {
        if (!map)
            return;

        // Stage 1: ensure grid index for every spawn (idempotent for already-indexed rows).
        for (ObjectGuid::LowType spawnId : spawnIds)
        {
            GameObjectData* data = const_cast<GameObjectData*>(sObjectMgr->GetGameObjectData(spawnId));
            if (!data)
            {
                if (errorOut.empty())
                    errorOut = Trinity::StringFormat("Spawn {} missing during SpawnNew publish", spawnId);
                continue;
            }
            sObjectMgr->AddGameobjectToGrid(data);
        }

        // Stage 2: create all loaded-grid instances. Prefer addToMap=false then AddToMap
        // only if we need a single visibility pass; CreateGameObjectFromDB(true) is fine
        // when the whole group is published in one map callback (this function).
        for (ObjectGuid::LowType spawnId : spawnIds)
        {
            GameObjectData const* data = sObjectMgr->GetGameObjectData(spawnId);
            if (!data || !map->IsGridLoaded(data->spawnPoint))
                continue;

            if (!GameObject::CreateGameObjectFromDB(spawnId, map, true))
            {
                if (errorOut.empty())
                    errorOut = Trinity::StringFormat("Failed to create spawn {} on map {}", spawnId, map->GetId());
            }
        }
    }

    void FinalizeTelemetry(GobGroupBatchTelemetry& telemetry, uint32 objectCount,
        uint32 sqlChunks, uint32 dbElapsedMs, uint32 runtimeElapsedMs, uint32 queueWaitMs,
        GobGroupBatchTelemetry::Result outcome)
    {
        telemetry.ObjectCount = objectCount;
        telemetry.SqlChunks = sqlChunks;
        telemetry.DbElapsedMs = dbElapsedMs;
        telemetry.RuntimeElapsedMs = runtimeElapsedMs;
        telemetry.QueueWaitMs = queueWaitMs;
        telemetry.Outcome = outcome;

        uint32 const totalMs = dbElapsedMs + runtimeElapsedMs + queueWaitMs;
        if (objectCount > 0 && totalMs > 0)
            telemetry.EffectiveGoPerSec = float(objectCount) * 1000.f / float(totalMs);
        else
            telemetry.EffectiveGoPerSec = 0.f;
    }
}
}
