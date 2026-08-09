#include "RoleplayCommandPhaseGuard.h"

#include "Chat.h"
#include "Object.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <fmt/format.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace
{
struct GobMoverAuthorization
{
    Player const* Viewer = nullptr;
    RoleplayPhaseSpawnType Type = RoleplayPhaseSpawnType::GameObject;
    uint64 SpawnId = 0;
    uint64 ViewerPhaseId = 0;
    uint64 SpawnPhaseId = 0;
    bool AllPhasesBypass = false;
    std::chrono::steady_clock::time_point ExpiresAt;
};

std::mutex GobMoverAuthorizationMutex;
std::unordered_map<uint64, GobMoverAuthorization> GobMoverAuthorizations;
constexpr std::chrono::minutes GobMoverAuthorizationLifetime{ 10 };
}

namespace RoleplayCommandPhaseGuard
{
std::string FormatPhaseTag(uint64 phaseId)
{
    return fmt::format("[RP phase: {}]", phaseId);
}

uint64 GetViewerPhaseId(Player const* viewer)
{
    if (!viewer)
        return 0;

    return sRoleplayPhaseMgr.GetPlayerPhaseId(viewer->GetGUID().GetCounter(), viewer->GetMapId());
}

uint64 GetSpawnPhaseId(Player const* viewer, RoleplayPhaseSpawnType type, uint64 spawnId)
{
    if (!viewer || !spawnId)
        return 0;

    return sRoleplayPhaseMgr.GetSpawnPhaseId(type, spawnId, viewer->GetMapId());
}

SpawnContext Resolve(Player const* viewer, RoleplayPhaseSpawnType type, uint64 spawnId)
{
    SpawnContext ctx;
    ctx.ViewerPhaseId = GetViewerPhaseId(viewer);
    ctx.SpawnPhaseId = GetSpawnPhaseId(viewer, type, spawnId);
    ctx.CrossPhase = !RoleplayPhaseMgr::SharesExclusiveContext(ctx.ViewerPhaseId, ctx.SpawnPhaseId);
    return ctx;
}

bool AllowsViewerSpawnContext(SpawnContext const& ctx, bool allPhasesBypass)
{
    return !ctx.CrossPhase || allPhasesBypass;
}

bool CanAccessLiveObject(Player const* viewer, WorldObject const* target, bool allPhasesBypass)
{
    if (!viewer || !target)
        return false;

    return allPhasesBypass || viewer->CanShareRoleplayContext(target);
}

bool ValidateAllPhasesBypass(Player* viewer, ChatHandler* handler, bool requested, std::string_view auditAction)
{
    if (!requested)
        return false;

    if (!viewer || !viewer->GetSession())
    {
        if (handler)
        {
            handler->SendSysMessage("Команда доступна только в игре.");
            handler->SetSentErrorMessage(true);
        }
        return false;
    }

    if (!viewer->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES))
    {
        if (handler)
        {
            handler->SendSysMessage("Для --all-phases требуется RBAC 3045 (staff phase access).");
            handler->SetSentErrorMessage(true);
        }
        return false;
    }

    sRoleplayPhaseMgr.WriteAudit(viewer->GetGUID().GetCounter(), viewer->GetSession()->GetAccountId(),
        auditAction, 0, R"({"source":"command","bypass":"all_phases"})");
    return true;
}

bool DenyCrossPhase(ChatHandler* handler, SpawnContext const& ctx, std::string_view hint)
{
    if (!handler)
        return false;

    handler->SendSysMessage(fmt::format(
        "Объект в logical RP phase {} (ваша: {}). {}",
        ctx.SpawnPhaseId, ctx.ViewerPhaseId, hint).c_str());
    handler->SetSentErrorMessage(true);
    return false;
}

bool AllowsViewerSpawnMutation(Player* viewer, ChatHandler* handler, RoleplayPhaseSpawnType type,
    uint64 spawnId, bool allPhasesBypass, std::string_view auditAction)
{
    SpawnContext const ctx = Resolve(viewer, type, spawnId);
    if (!viewer || !viewer->GetSession())
        return false;

    uint64 const targetPhaseId = ctx.SpawnPhaseId;
    uint64 const characterGuid = viewer->GetGUID().GetCounter();
    uint32 const accountId = viewer->GetSession()->GetAccountId();
    bool const serverStaff = sRoleplayPhaseMgr.CanMutateCommonWorld(viewer->GetSession()->GetSecurity());

    // Context 0 is the common world, not an rp_phase row: never grant player
    // build rights there merely because the spawn has no mapping.
    if (targetPhaseId == 0 && !serverStaff)
    {
        if (handler)
        {
            handler->SendSysMessage("Мутации persistent spawn в common world (context 0) требуют GM1+.");
            handler->SetSentErrorMessage(true);
        }
        return false;
    }

    if (ctx.CrossPhase && !allPhasesBypass)
        return DenyCrossPhase(handler, ctx,
            "Cross-phase мутация запрещена. Staff: добавьте --all-phases confirm.");

    if (ctx.CrossPhase && !viewer->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES))
    {
        if (handler)
        {
            handler->SendSysMessage("Для cross-phase мутации требуется RBAC 3045.");
            handler->SetSentErrorMessage(true);
        }
        return false;
    }

    if (targetPhaseId && !sRoleplayPhaseMgr.CanEdit(targetPhaseId, characterGuid, accountId, allPhasesBypass, serverStaff))
    {
        if (handler)
        {
            handler->SendSysMessage("Для мутации в этой RP phase требуется роль editor, manager или owner.");
            handler->SetSentErrorMessage(true);
        }
        return false;
    }

    sRoleplayPhaseMgr.WriteAudit(viewer->GetGUID().GetCounter(), viewer->GetSession()->GetAccountId(),
        auditAction, ctx.SpawnPhaseId,
        fmt::format(R"({{"spawn_id":{},"viewer_phase":{},"target_phase":{},"bypass":"{}"}})",
            spawnId, ctx.ViewerPhaseId, targetPhaseId, allPhasesBypass ? "all_phases" : "none"));
    return true;
}

bool AuthorizeGobMoverTarget(Player* viewer, RoleplayPhaseSpawnType type, uint64 spawnId, bool allPhasesBypass)
{
    if (!viewer || !viewer->GetSession() || !spawnId)
        return false;

    SpawnContext const ctx = Resolve(viewer, type, spawnId);
    if (ctx.CrossPhase && (!allPhasesBypass
        || !viewer->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES)))
        return false;

    GobMoverAuthorization authorization;
    authorization.Viewer = viewer;
    authorization.Type = type;
    authorization.SpawnId = spawnId;
    authorization.ViewerPhaseId = ctx.ViewerPhaseId;
    authorization.SpawnPhaseId = ctx.SpawnPhaseId;
    authorization.AllPhasesBypass = allPhasesBypass;
    authorization.ExpiresAt = std::chrono::steady_clock::now() + GobMoverAuthorizationLifetime;

    std::lock_guard<std::mutex> lock(GobMoverAuthorizationMutex);
    GobMoverAuthorizations[viewer->GetGUID().GetCounter()] = authorization;
    return true;
}

void RevokeGobMoverAuthorization(Player const* viewer)
{
    if (!viewer)
        return;

    std::lock_guard<std::mutex> lock(GobMoverAuthorizationMutex);
    GobMoverAuthorizations.erase(viewer->GetGUID().GetCounter());
}

bool AllowsAuthorizedGobMoverMutation(Player* viewer, RoleplayPhaseSpawnType type, uint64 spawnId,
    std::string_view auditAction)
{
    if (!viewer || !viewer->GetSession() || !spawnId)
        return false;

    SpawnContext const ctx = Resolve(viewer, type, spawnId);
    if (!ctx.CrossPhase)
        return AllowsViewerSpawnMutation(viewer, nullptr, type, spawnId, false, auditAction);

    bool authorized = false;
    {
        std::lock_guard<std::mutex> lock(GobMoverAuthorizationMutex);
        auto authorization = GobMoverAuthorizations.find(viewer->GetGUID().GetCounter());
        if (authorization != GobMoverAuthorizations.end())
        {
            if (authorization->second.ExpiresAt <= std::chrono::steady_clock::now())
                GobMoverAuthorizations.erase(authorization);
            else
                authorized = authorization->second.Type == type
                    && authorization->second.Viewer == viewer
                    && authorization->second.SpawnId == spawnId
                    && authorization->second.ViewerPhaseId == ctx.ViewerPhaseId
                    && authorization->second.SpawnPhaseId == ctx.SpawnPhaseId
                    && authorization->second.AllPhasesBypass;
        }
    }

    return authorized && AllowsViewerSpawnMutation(viewer, nullptr, type, spawnId, true, auditAction);
}
}
