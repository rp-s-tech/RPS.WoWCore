/*
 * NobleNext — same-tick runtime batch for GobGroup / GobBlueprint.
 *
 * Contract:
 *   RelocateExisting — Hide → ApplyAll → RevealAll without yield (same map).
 *   SpawnNew         — Publish staged GameObjectData → grid → RevealAll.
 *
 * One heavy runtime publish per map at a time; whole groups only (no member split).
 */

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "noble_next_gobgroup_transform.h"

#include <string>
#include <vector>

class Map;

namespace RoleplayCore::NobleNext
{
    static constexpr uint32 GOBGROUP_MAP_RUNTIME_BUDGET_MS = 5;

    struct GobGroupBatchTelemetry
    {
        enum class Result : uint8
        {
            None = 0,
            Completed,
            Failed,
            Compensated
        };

        uint32 ObjectCount = 0;
        uint32 SqlChunks = 0;
        uint32 DbElapsedMs = 0;
        uint32 RuntimeElapsedMs = 0;
        uint32 QueueWaitMs = 0;
        float EffectiveGoPerSec = 0.f;
        Result Outcome = Result::None;
    };

    namespace GobGroupBatch
    {
        void ApplyPlanToCache(GroupTransformPlan const& plan, std::string& errorOut);

        void HideLive(Map* map, GroupTransformPlan const& plan);

        void RevealAll(Map* map, GroupTransformPlan const& plan, std::string& errorOut);

        // Same-map relocate: Hide → ApplyAll → RevealAll in one call (no yield).
        void RelocateExisting(Map* map, GroupTransformPlan const& plan, std::string& errorOut);

        // Blueprint / new spawn: add staged data to grid and create loaded-grid instances.
        void SpawnNew(Map* map, std::vector<ObjectGuid::LowType> const& spawnIds, std::string& errorOut);

        void FinalizeTelemetry(GobGroupBatchTelemetry& telemetry, uint32 objectCount,
            uint32 sqlChunks, uint32 dbElapsedMs, uint32 runtimeElapsedMs, uint32 queueWaitMs,
            GobGroupBatchTelemetry::Result outcome);
    }
}
