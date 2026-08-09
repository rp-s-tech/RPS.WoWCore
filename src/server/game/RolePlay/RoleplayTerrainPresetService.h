#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "TerrainPreset.h"

#include <mutex>
#include <unordered_map>

class Player;
class WorldObject;

// Owns exactly one VisibleMapId reference per player for RP terrain presets.
// External quest/condition VisibleMapId references are never wiped with while-remove.
class TC_GAME_API RoleplayTerrainPresetService
{
public:
    static RoleplayTerrainPresetService& Instance();

    // Applies a VisibleMapSwap preset. TeleportOnly catalog entries (e.g. 2927)
    // are rejected here; teleport flows own that path separately.
    bool Apply(Player* player, TerrainPresetId presetId);

    // Removes only the service-owned reference, if any.
    void ClearOwned(Player* player);

    // Map changes rebuild native VisibleMapIds; drop ownership tracking.
    void OnMapChange(Player* player);

    // Player leaving world: forget ownership without mutating PhaseShift.
    void Forget(Player* player);

    uint32 GetOwnedVisibleMapId(Player const* player) const;

private:
    void ClearOwnedLocked(Player* player);

    mutable std::mutex _mutex;
    std::unordered_map<ObjectGuid, uint32> _ownedVisibleMapId;
};

#define sRoleplayTerrainPresetService RoleplayTerrainPresetService::Instance()
