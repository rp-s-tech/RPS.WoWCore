#pragma once

#include "Define.h"

class Player;

// Side effects that must accompany a logical RP context change for an online
// player. Snapshot/DB mutation stays in RoleplayPhaseMgr; this helper only
// owns combat/gossip/loot cleanup, visibility refresh and the unlocked
// protocol callback.
namespace RoleplayPlayerTransitionEffects
{
    TC_GAME_API void Cleanup(Player* player);
    TC_GAME_API void RefreshVisibility(Player* player);

    // Runs cleanup + visibility refresh, then optionally notifies the protocol
    // handler. Must be called without RoleplayPhaseMgr::_mutationMutex held.
    TC_GAME_API void Apply(Player* player, uint64 previousPhaseId, uint64 currentPhaseId,
        void (*handler)(Player* player, uint64 previousPhaseId, uint64 currentPhaseId) = nullptr);
}
