#include "RoleplayTerrainPresetService.h"

#include "ObjectMgr.h"
#include "PhasingHandler.h"
#include "Player.h"

RoleplayTerrainPresetService& RoleplayTerrainPresetService::Instance()
{
    static RoleplayTerrainPresetService instance;
    return instance;
}

uint32 RoleplayTerrainPresetService::GetOwnedVisibleMapId(Player const* player) const
{
    if (!player)
        return 0;

    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _ownedVisibleMapId.find(player->GetGUID());
    return it != _ownedVisibleMapId.end() ? it->second : 0;
}

void RoleplayTerrainPresetService::ClearOwnedLocked(Player* player)
{
    auto it = _ownedVisibleMapId.find(player->GetGUID());
    if (it == _ownedVisibleMapId.end())
        return;

    uint32 const ownedVisibleMapId = it->second;
    _ownedVisibleMapId.erase(it);

    // Drop exactly one owned reference. Never while-remove foreign refs.
    if (ownedVisibleMapId && player->GetPhaseShift().HasVisibleMapId(ownedVisibleMapId))
        PhasingHandler::RemoveVisibleMapId(player, ownedVisibleMapId);
}

void RoleplayTerrainPresetService::ClearOwned(Player* player)
{
    if (!player)
        return;

    std::lock_guard<std::mutex> lock(_mutex);
    ClearOwnedLocked(player);
}

void RoleplayTerrainPresetService::OnMapChange(Player* player)
{
    Forget(player);
}

void RoleplayTerrainPresetService::Forget(Player* player)
{
    if (!player)
        return;

    // PhasingHandler::OnMapChange / logout already owns PhaseShift lifetime.
    // Only drop the service-side ownership bookkeeping here.
    std::lock_guard<std::mutex> lock(_mutex);
    _ownedVisibleMapId.erase(player->GetGUID());
}

bool RoleplayTerrainPresetService::Apply(Player* player, TerrainPresetId presetId)
{
    TerrainPreset const* preset = GetTerrainPreset(presetId);
    if (!player || !preset)
        return false;

    // Teleport-only catalog rows are not VisibleMapId owners.
    if (!preset->IsVisibleMapSwap() || !preset->VisibleMapId)
        return false;

    if (player->GetMapId() != preset->ParentMapId || !sObjectMgr->GetTerrainSwapInfo(preset->VisibleMapId))
        return false;

    std::lock_guard<std::mutex> lock(_mutex);

    uint32 const previousOwned = [&]() -> uint32
    {
        auto it = _ownedVisibleMapId.find(player->GetGUID());
        return it != _ownedVisibleMapId.end() ? it->second : 0;
    }();

    if (previousOwned == preset->VisibleMapId)
        return true;

    if (previousOwned)
        ClearOwnedLocked(player);

    PhasingHandler::AddVisibleMapId(player, preset->VisibleMapId);
    _ownedVisibleMapId[player->GetGUID()] = preset->VisibleMapId;

    if (player->IsInWorld())
        player->UpdateObjectVisibility();

    return true;
}
