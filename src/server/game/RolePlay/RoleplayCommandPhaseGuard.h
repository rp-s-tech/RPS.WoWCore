/*
 * Shared current-phase policy for GM object/NPC/GobGroup commands.
 * Logical context 0 (no rp_phase_spawn) is the common world.
 */

#pragma once

#include "Define.h"
#include "RoleplayPhaseMgr.h"

#include <string>
#include <string_view>

class ChatHandler;
class Player;
class WorldObject;

namespace RoleplayCommandPhaseGuard
{
    struct SpawnContext
    {
        uint64 ViewerPhaseId = 0;
        uint64 SpawnPhaseId = 0;
        bool CrossPhase = false;
    };

    // Formats the shared "[RP phase: N]" suffix for command output.
    TC_GAME_API std::string FormatPhaseTag(uint64 phaseId);

    TC_GAME_API uint64 GetViewerPhaseId(Player const* viewer);
    TC_GAME_API uint64 GetSpawnPhaseId(Player const* viewer, RoleplayPhaseSpawnType type, uint64 spawnId);

    TC_GAME_API SpawnContext Resolve(Player const* viewer, RoleplayPhaseSpawnType type, uint64 spawnId);

    // Search/list: same logical context only, unless an audited staff bypass is active.
    TC_GAME_API bool AllowsViewerSpawnContext(SpawnContext const& ctx, bool allPhasesBypass);

    // Live object access (wraps CanShareRoleplayContext).
    TC_GAME_API bool CanAccessLiveObject(Player const* viewer, WorldObject const* target, bool allPhasesBypass);

    // Validates RBAC 3045 for an explicit --all-phases request and audits it.
    TC_GAME_API bool ValidateAllPhasesBypass(Player* viewer, ChatHandler* handler, bool requested,
        std::string_view auditAction);

    // Mutation gate: same context, or audited staff bypass. Fail-closed before DB/runtime writes.
    TC_GAME_API bool AllowsViewerSpawnMutation(Player* viewer, ChatHandler* handler,
        RoleplayPhaseSpawnType type, uint64 spawnId, bool allPhasesBypass, std::string_view auditAction);

    TC_GAME_API bool AuthorizeGobMoverTarget(Player* viewer, RoleplayPhaseSpawnType type, uint64 spawnId,
        bool allPhasesBypass);
    TC_GAME_API void RevokeGobMoverAuthorization(Player const* viewer);
    TC_GAME_API bool AllowsAuthorizedGobMoverMutation(Player* viewer, RoleplayPhaseSpawnType type, uint64 spawnId,
        std::string_view auditAction);

    TC_GAME_API bool DenyCrossPhase(ChatHandler* handler, SpawnContext const& ctx, std::string_view hint);
}
