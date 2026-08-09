#pragma once

#include "Define.h"

#include <cstdint>

// These IDs identify entries in the server-side terrain preset catalog. They
// deliberately are not Phase.db2 IDs and must never be used as logical RP phase IDs.
enum class TerrainPresetId : uint8
{
    Stromgarde,
    HordeGarrisonLevel1,
    HordeGarrisonLevel2,
    HordeGarrisonLevel3,
    AllianceGarrisonLevel1,
    AllianceGarrisonLevel2,
    AllianceGarrisonLevel3,
    ArathiHighlandsRpeTeleport
};

enum class TerrainPresetGroup : uint8
{
    None,
    GarrisonVariant
};

enum class TerrainPresetMode : uint8
{
    VisibleMapSwap,
    TeleportOnly
};

struct TerrainPreset
{
    TerrainPresetId Id;
    TerrainPresetGroup Group;
    TerrainPresetMode Mode;
    uint32 ParentMapId;
    uint32 MapId;
    uint32 VisibleMapId;
    char const* Name;

    constexpr bool IsVisibleMapSwap() const
    {
        return Mode == TerrainPresetMode::VisibleMapSwap;
    }
};

TC_GAME_API TerrainPreset const* GetTerrainPreset(TerrainPresetId presetId);
TC_GAME_API TerrainPreset const* GetTerrainPresetForVisibleMapId(uint32 visibleMapId);
