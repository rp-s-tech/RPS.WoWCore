#include "TerrainPreset.h"

#include <array>

namespace
{
constexpr std::array TerrainPresets =
{
    TerrainPreset{ TerrainPresetId::Stromgarde, TerrainPresetGroup::None, TerrainPresetMode::VisibleMapSwap, 0, 1945, 1945, "Stromgarde" },
    TerrainPreset{ TerrainPresetId::HordeGarrisonLevel1, TerrainPresetGroup::GarrisonVariant, TerrainPresetMode::VisibleMapSwap, 1116, 1152, 1152, "Horde Garrison Level 1" },
    TerrainPreset{ TerrainPresetId::HordeGarrisonLevel2, TerrainPresetGroup::GarrisonVariant, TerrainPresetMode::VisibleMapSwap, 1116, 1153, 1153, "Horde Garrison Level 2" },
    TerrainPreset{ TerrainPresetId::HordeGarrisonLevel3, TerrainPresetGroup::GarrisonVariant, TerrainPresetMode::VisibleMapSwap, 1116, 1154, 1154, "Horde Garrison Level 3" },
    TerrainPreset{ TerrainPresetId::AllianceGarrisonLevel1, TerrainPresetGroup::GarrisonVariant, TerrainPresetMode::VisibleMapSwap, 1116, 1158, 1158, "Alliance Garrison Level 1" },
    TerrainPreset{ TerrainPresetId::AllianceGarrisonLevel2, TerrainPresetGroup::GarrisonVariant, TerrainPresetMode::VisibleMapSwap, 1116, 1159, 1159, "Alliance Garrison Level 2" },
    TerrainPreset{ TerrainPresetId::AllianceGarrisonLevel3, TerrainPresetGroup::GarrisonVariant, TerrainPresetMode::VisibleMapSwap, 1116, 1160, 1160, "Alliance Garrison Level 3" },
    TerrainPreset{ TerrainPresetId::ArathiHighlandsRpeTeleport, TerrainPresetGroup::None, TerrainPresetMode::TeleportOnly, 0, 2927, 0, "Arathi Highlands RPE" }
};

static_assert(TerrainPresets.back().Mode == TerrainPresetMode::TeleportOnly);
static_assert(TerrainPresets.back().MapId == 2927);
static_assert(TerrainPresets.back().VisibleMapId == 0);
}

TerrainPreset const* GetTerrainPreset(TerrainPresetId presetId)
{
    for (TerrainPreset const& preset : TerrainPresets)
        if (preset.Id == presetId)
            return &preset;

    return nullptr;
}

TerrainPreset const* GetTerrainPresetForVisibleMapId(uint32 visibleMapId)
{
    for (TerrainPreset const& preset : TerrainPresets)
        if (preset.IsVisibleMapSwap() && preset.VisibleMapId == visibleMapId)
            return &preset;

    return nullptr;
}
