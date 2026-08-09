/*
 * NobleNext — .gobject group * (spatial soft groups).
 *
 * Nested under vanilla `gobject` so `.gob group …` resolves via TC prefix matching.
 *
 * Group-scoped commands take <group-guid> first (root or any member → canonical root).
 * Mutations never depend on active group / last targeted GO.
 *
 * Structured addon replies use prefix NN_GOBGROUP (see noble_next_gobgroup_protocol.*).
 */

#include "noble_next_gobgroup_mgr.h"
#include "noble_next_gobgroup_protocol.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayPhaseMgr.h"
#include "WorldSession.h"

#include <cctype>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    using GameObjectSpawnId = Variant<Hyperlink<gameobject>, ObjectGuid::LowType>;

    class GobGroupCommands : public CommandScript
    {
    public:
        GobGroupCommands() : CommandScript("noble_next_gobgroup_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable gobjectGroupTable =
            {
                { "help",         HandleHelp,         LANG_COMMAND_GOBGROUP_HELP,         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_HELP,     Console::No },
                { "capabilities", HandleCapabilities, LANG_COMMAND_GOBGROUP_HELP,         rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP,          Console::No },
                { "create",       HandleCreate,       LANG_COMMAND_GOBGROUP_CREATE_HELP,  rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CREATE,   Console::No },
                { "use",          HandleUse,          LANG_COMMAND_GOBGROUP_USE_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_USE,      Console::No },
                { "add",          HandleAdd,          LANG_COMMAND_GOBGROUP_ADD_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_ADD,      Console::No },
                { "scan",         HandleScan,         LANG_COMMAND_GOBGROUP_ADD_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_SCAN,     Console::No },
                { "addnear",      HandleAddNear,      LANG_COMMAND_GOBGROUP_ADD_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_ADDNEAR,  Console::No },
                { "remove",       HandleRemove,       LANG_COMMAND_GOBGROUP_REMOVE_HELP,  rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_REMOVE,   Console::No },
                { "dissolve",     HandleDissolve,     LANG_COMMAND_GOBGROUP_REMOVE_HELP,  rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_DISSOLVE, Console::No },
                { "delete",       HandleDelete,       LANG_COMMAND_GOBGROUP_REMOVE_HELP,  rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_DELETE,   Console::No },
                { "info",         HandleInfo,         LANG_COMMAND_GOBGROUP_INFO_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_INFO,     Console::No },
                { "list",         HandleList,         LANG_COMMAND_GOBGROUP_INFO_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_LIST,     Console::No },
                { "near",         HandleNear,         LANG_COMMAND_GOBGROUP_INFO_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_LIST,     Console::No },
                { "target",       HandleTarget,       LANG_COMMAND_GOBGROUP_USE_HELP,     rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_TARGET,   Console::No },
                { "check",        HandleCheck,        LANG_COMMAND_GOBGROUP_INFO_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CHECK,    Console::No },
                { "status",       HandleStatus,       LANG_COMMAND_GOBGROUP_INFO_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_STATUS,   Console::No },
                { "capture",      HandleCapture,      LANG_COMMAND_GOBGROUP_CAPTURE_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CAPTURE,  Console::No },
                { "recalc",       HandleRecalc,       LANG_COMMAND_GOBGROUP_RECALC_HELP,  rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_RECALC,   Console::No },
                { "sync",         HandleSync,         LANG_COMMAND_GOBGROUP_SYNC_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_SYNC,     Console::No },
                { "move",         HandleMove,         LANG_COMMAND_GOBGROUP_MOVE_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_MOVE,     Console::No },
                { "turn",         HandleTurn,         LANG_COMMAND_GOBGROUP_MOVE_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_TURN,     Console::No },
                { "nudge",        HandleNudge,        LANG_COMMAND_GOBGROUP_MOVE_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_MOVE,     Console::No },
                { "rotate",       HandleRotate,       LANG_COMMAND_GOBGROUP_MOVE_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_TURN,     Console::No },
                { "scale",        HandleScale,        LANG_COMMAND_GOBGROUP_MOVE_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_MOVE,     Console::No },
                { "relocate",     HandleRelocate,     LANG_COMMAND_GOBGROUP_MOVE_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_RELOCATE, Console::No },
                { "reload",       HandleReload,       LANG_COMMAND_GOBGROUP_INFO_HELP,    rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_RELOAD,   Console::No },
                { "cleanup",      HandleCleanup,      LANG_COMMAND_GOBGROUP_CLEANUP_HELP, rbac::RBAC_PERM_COMMAND_GOBJECT_GROUP_CLEANUP,  Console::No },
            };

            static ChatCommandTable gobjectTable =
            {
                { "group", gobjectGroupTable },
            };

            static ChatCommandTable commandTable =
            {
                { "gobject", gobjectTable },
            };
            return commandTable;
        }

    private:
        static Player* PlayerOf(ChatHandler* handler)
        {
            return handler ? handler->GetPlayer() : nullptr;
        }

        static bool RequirePlayer(ChatHandler* handler, Player*& player, std::string_view verb)
        {
            player = handler->GetPlayer();
            if (!player)
            {
                GobGroupProtocol::SendResult(nullptr, verb, "error", "player required");
                return false;
            }
            return true;
        }

        static bool ResolveRequestedBypass(Player* player, ChatHandler* handler, bool requested,
            std::string_view auditAction, bool& bypass)
        {
            bypass = RoleplayCommandPhaseGuard::ValidateAllPhasesBypass(
                player, handler, requested, auditAction);
            return !requested || bypass;
        }

        static bool ResolveGroupRoot(ChatHandler* handler, Player* player, ObjectGuid::LowType anyGuid, ObjectGuid::LowType& outRoot,
            std::string_view verb, bool allPhasesBypass = false, bool forMutation = false, std::string_view auditAction = {})
        {
            auto checkSpawn = [&](ObjectGuid::LowType spawnId) -> bool
            {
                if (forMutation)
                {
                    if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, handler,
                        RoleplayPhaseSpawnType::GameObject, spawnId, allPhasesBypass, auditAction))
                        return false;
                }
                else
                {
                    RoleplayCommandPhaseGuard::SpawnContext const ctx = RoleplayCommandPhaseGuard::Resolve(
                        player, RoleplayPhaseSpawnType::GameObject, spawnId);
                    if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(ctx, allPhasesBypass))
                    {
                        RoleplayCommandPhaseGuard::DenyCrossPhase(handler, ctx,
                            fmt::format("Команда .gobject group {} недоступна для другой logical RP phase.", verb));
                        return false;
                    }
                }
                return true;
            };

            if (!checkSpawn(anyGuid))
                return false;

            outRoot = sGobGroupMgr.FindRootGuid(anyGuid);
            if (!outRoot)
            {
                std::string const detail = fmt::format("GO {} не состоит в группе.", anyGuid);
                handler->SendSysMessage(detail.c_str());
                GobGroupProtocol::SendResult(player, verb, "error", detail);
                handler->SetSentErrorMessage(true);
                return false;
            }

            if (outRoot != anyGuid && !checkSpawn(outRoot))
                return false;

            return true;
        }

        static bool CheckMemberMutation(ChatHandler* handler, Player* player, ObjectGuid::LowType spawnId,
            std::string_view auditAction, bool allPhasesBypass)
        {
            return RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, handler,
                RoleplayPhaseSpawnType::GameObject, spawnId, allPhasesBypass, auditAction);
        }

        static constexpr float GOBGROUP_MAX_SCAN_RADIUS = 100.f;
        static constexpr float GOBGROUP_DEFAULT_NEAR_RADIUS = 50.f;
        static constexpr float GOBGROUP_MAX_NEAR_RADIUS = 1000.f;

        static void ActivateGroup(Player* player, ObjectGuid::LowType root)
        {
            sGobGroupMgr.SetActiveRoot(player->GetSession()->GetAccountId(), root);
        }

        static void RememberTargetedGo(Player* player, ObjectGuid::LowType root)
        {
            if (!player)
                return;

            // Legacy 32-bit last-target field cannot hold full BIGINT spawn GUIDs.
            if (root <= std::numeric_limits<uint32>::max())
                player->SetLastTargetedGO(uint32(root));
            player->SetLastTargetedGO2(root);
        }

        static bool ValidateRadius(ChatHandler* handler, Player* player, std::string_view verb, float radius)
        {
            if (!std::isfinite(radius) || radius <= 0.f || radius > GOBGROUP_MAX_SCAN_RADIUS)
            {
                std::string const detail = fmt::format(
                    "Радиус должен быть в диапазоне (0, {}]. Использование: .gobject group {} <group-guid> <radius>{}",
                    GOBGROUP_MAX_SCAN_RADIUS, verb, verb == "addnear" ? " confirm" : "");
                handler->SendSysMessage(detail.c_str());
                handler->SetSentErrorMessage(true);
                GobGroupProtocol::SendResult(player, verb, "error", detail);
                return false;
            }
            return true;
        }

        static bool ValidateNearRadius(ChatHandler* handler, Player* player, float radius)
        {
            if (!std::isfinite(radius) || radius <= 0.f || radius > GOBGROUP_MAX_NEAR_RADIUS)
            {
                std::string const detail = fmt::format(
                    "Радиус должен быть в диапазоне (0, {}]. Использование: .gobject group near [radius]",
                    GOBGROUP_MAX_NEAR_RADIUS);
                handler->SendSysMessage(detail.c_str());
                handler->SetSentErrorMessage(true);
                GobGroupProtocol::SendResult(player, "near", "error", detail);
                return false;
            }
            return true;
        }

        static void SendReport(ChatHandler* handler, std::string const& text)
        {
            if (text.empty())
                return;
            handler->SendSysMessage(text.c_str());
        }

        static bool Fail(ChatHandler* handler, std::string_view verb, std::string const& error)
        {
            if (!error.empty())
                handler->SendSysMessage(error.c_str());
            GobGroupProtocol::SendResult(PlayerOf(handler), verb, "error", error);
            handler->SetSentErrorMessage(true);
            return false;
        }

        static bool Ok(ChatHandler* handler, std::string_view verb, std::string_view detail = {})
        {
            GobGroupProtocol::SendResult(PlayerOf(handler), verb, "ok", detail);
            return true;
        }

        static bool Queued(ChatHandler* handler, std::string_view verb, ObjectGuid::LowType root,
            std::string_view detail = {})
        {
            GobGroupProtocol::SendResult(PlayerOf(handler), verb, "queued", detail);
            GobGroupStatusSnapshot status;
            std::string error;
            if (sGobGroupMgr.TryGetStatusSnapshot(root, status, error))
                GobGroupProtocol::SendStatus(PlayerOf(handler), status);
            return true;
        }

        static bool HandleHelp(ChatHandler* handler)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "help"))
                return false;

            handler->SendSysMessage(".gobject group — spatial soft groups (<group-guid> = root или member → canonical root):");
            handler->SendSysMessage("  .gobject group capabilities — NN_GOBGROUP CAPS snapshot");
            handler->SendSysMessage("  .gobject group create <root guid> [name]");
            handler->SendSysMessage("  .gobject group use|target <group-guid>");
            handler->SendSysMessage("  .gobject group add <group-guid> <member-guid>");
            handler->SendSysMessage("  .gobject group scan <group-guid> <radius>");
            handler->SendSysMessage("  .gobject group addnear <group-guid> <radius> confirm");
            handler->SendSysMessage("  .gobject group remove <group-guid> <member-guid>");
            handler->SendSysMessage("  .gobject group dissolve <group-guid> | delete <group-guid> full-force");
            handler->SendSysMessage("  .gobject group info <object-guid> | list [map] | near [radius]");
            handler->SendSysMessage("  .gobject group check <group-guid> | .gobject group status <group-guid>");
            handler->SendSysMessage("  .gobject group capture <guid> [silent] | recalc <group-guid> | sync <group-guid>");
            handler->SendSysMessage("  .gobject group move <group-guid> [x y z] | turn <group-guid> [o]");
            handler->SendSysMessage("  .gobject group nudge <group-guid> <dx dy dz> | nudge <group-guid> <F|FR|…|UP|DOWN> <step>");
            handler->SendSysMessage("  .gobject group rotate <group-guid> <dYawDeg> [dPitchDeg] [dRollDeg]");
            handler->SendSysMessage("  .gobject group scale <group-guid> <factor>");
            handler->SendSysMessage("  .gobject group relocate <group-guid> <map> <x> <y> <z> <o> confirm");
            handler->SendSysMessage("  .gobject group reload | .gobject group cleanup confirm");
            return Ok(handler, "help");
        }

        static bool HandleCapabilities(ChatHandler* handler)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "capabilities"))
                return false;

            GobGroupProtocol::SendCapabilities(player);
            return Ok(handler, "capabilities");
        }

        static bool HandleCreate(ChatHandler* handler, GameObjectSpawnId rootSpawnId, Optional<Tail> name,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "create"))
                return false;

            ObjectGuid::LowType const rootGuid = *rootSpawnId;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_create", bypass))
                return false;
            if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, handler,
                RoleplayPhaseSpawnType::GameObject, rootGuid, bypass, "gobgroup_create"))
                return false;
            std::string const groupName = name
                ? std::string(name->data(), name->size())
                : std::string();
            std::string error;
            if (!sGobGroupMgr.Create(player, rootGuid, groupName, error))
                return Fail(handler, "create", error);

            ActivateGroup(player, rootGuid);
            handler->SendSysMessage(fmt::format(".gobject group create: root {} активен.", rootGuid).c_str());
            return Ok(handler, "create", fmt::format("{}", rootGuid));
        }

        static bool HandleUse(ChatHandler* handler, GameObjectSpawnId guid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "use"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *guid, root, "use"))
                return false;

            ActivateGroup(player, root);
            RememberTargetedGo(player, root);
            handler->SendSysMessage(fmt::format("Активная группа: root {}.", root).c_str());
            return Ok(handler, "use", fmt::format("{}", root));
        }

        static bool HandleTarget(ChatHandler* handler, GameObjectSpawnId guid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "target"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *guid, root, "target"))
                return false;

            ActivateGroup(player, root);
            RememberTargetedGo(player, root);
            handler->SendSysMessage(fmt::format("Активная группа: root {}.", root).c_str());
            return Ok(handler, "target", fmt::format("{}", root));
        }

        static bool HandleAdd(ChatHandler* handler, GameObjectSpawnId groupGuid, GameObjectSpawnId memberGuid,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "add"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_add", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "add", bypass, true, "gobgroup_add"))
                return false;

            ObjectGuid::LowType const member = *memberGuid;
            if (!CheckMemberMutation(handler, player, member, "gobgroup_add", bypass))
                return false;
            std::string error;
            if (!sGobGroupMgr.AddMember(root, member, error))
                return Fail(handler, "add", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(fmt::format("Member {} добавлен в группу root {}.", member, root).c_str());
            return Ok(handler, "add", fmt::format("{}:{}", root, member));
        }

        static bool HandleScan(ChatHandler* handler, GameObjectSpawnId groupGuid, float radius)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "scan"))
                return false;

            if (!ValidateRadius(handler, player, "scan", radius))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "scan"))
                return false;

            std::vector<ObjectGuid::LowType> candidates;
            std::string report;
            uint32 count = sGobGroupMgr.ScanNear(player, root, radius, candidates, report);
            SendReport(handler, report);
            handler->SendSysMessage(fmt::format(".gobject group scan: кандидатов {}.", count).c_str());
            return Ok(handler, "scan", fmt::format("{}", count));
        }

        static bool HandleAddNear(ChatHandler* handler, GameObjectSpawnId groupGuid, float radius, EXACT_SEQUENCE("confirm"),
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "addnear"))
                return false;

            if (!ValidateRadius(handler, player, "addnear", radius))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_addnear", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "addnear", bypass, true, "gobgroup_addnear"))
                return false;

            std::string report;
            if (!sGobGroupMgr.AddNear(player, root, radius, true, report))
                return Fail(handler, "addnear", report);

            ActivateGroup(player, root);
            SendReport(handler, report);
            return Ok(handler, "addnear", fmt::format("{}", root));
        }

        static bool HandleRemove(ChatHandler* handler, GameObjectSpawnId groupGuid, GameObjectSpawnId memberGuid,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "remove"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_remove", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "remove", bypass, true, "gobgroup_remove"))
                return false;

            ObjectGuid::LowType const member = *memberGuid;
            if (!CheckMemberMutation(handler, player, member, "gobgroup_remove", bypass))
                return false;
            std::string error;
            if (!sGobGroupMgr.RemoveMember(root, member, error))
                return Fail(handler, "remove", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(fmt::format("Member {} удалён из группы root {} (GO остаётся).", member, root).c_str());
            return Ok(handler, "remove", fmt::format("{}:{}", root, member));
        }

        static bool HandleDissolve(ChatHandler* handler, GameObjectSpawnId groupGuid,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "dissolve"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_dissolve", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "dissolve", bypass, true, "gobgroup_dissolve"))
                return false;

            std::string error;
            if (!sGobGroupMgr.Dissolve(root, error))
                return Fail(handler, "dissolve", error);

            if (sGobGroupMgr.GetActiveRoot(player->GetSession()->GetAccountId()) == root)
                ActivateGroup(player, 0);
            handler->SendSysMessage(fmt::format("Группа root {} распущена (GO остаются).", root).c_str());
            return Ok(handler, "dissolve", fmt::format("{}", root));
        }

        static bool HandleDelete(ChatHandler* handler, GameObjectSpawnId groupGuid, EXACT_SEQUENCE("full-force"),
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "delete"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_delete", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "delete", bypass, true, "gobgroup_delete"))
                return false;

            std::string report;
            if (!sGobGroupMgr.DeleteFull(root, true, report))
                return Fail(handler, "delete", report);

            SendReport(handler, report);
            return Ok(handler, "delete", fmt::format("{}", root));
        }

        static bool HandleInfo(ChatHandler* handler, GameObjectSpawnId objectGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "info"))
                return false;

            ObjectGuid::LowType const guid = *objectGuid;
            RoleplayCommandPhaseGuard::SpawnContext const ctx = RoleplayCommandPhaseGuard::Resolve(
                player, RoleplayPhaseSpawnType::GameObject, guid);
            if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(ctx, false))
                return Fail(handler, "info", fmt::format("GO {} is in another logical RP phase.", guid));

            if (ObjectGuid::LowType const root = sGobGroupMgr.FindRootGuid(guid))
                ActivateGroup(player, root);

            SendReport(handler, sGobGroupMgr.BuildInfo(player, guid));

            GobGroupInfoSnapshot snap;
            std::string error;
            if (!sGobGroupMgr.TryGetInfoSnapshot(player, guid, snap, error))
            {
                GobGroupProtocol::SendResult(player, "info", "error", error);
                handler->SetSentErrorMessage(true);
                return false;
            }

            GobGroupProtocol::SendInfo(player, snap);
            return Ok(handler, "info", fmt::format("{}", guid));
        }

        static bool HandleList(ChatHandler* handler, Optional<uint32> mapId)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "list"))
                return false;

            SendReport(handler, sGobGroupMgr.BuildList(player, mapId));

            GobGroupListSnapshot snap;
            sGobGroupMgr.GetListSnapshot(player, mapId, snap);
            GobGroupProtocol::SendList(player, snap);
            return Ok(handler, "list", mapId ? fmt::format("{}", *mapId) : "all");
        }

        static bool HandleNear(ChatHandler* handler, Optional<float> requestedRadius)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "near"))
                return false;

            float const radius = requestedRadius.value_or(GOBGROUP_DEFAULT_NEAR_RADIUS);
            if (!ValidateNearRadius(handler, player, radius))
                return false;

            GobGroupNearSnapshot snap;
            sGobGroupMgr.GetNearSnapshot(player, radius, snap);
            GobGroupProtocol::SendNear(player, snap);
            handler->SendSysMessage(fmt::format(
                ".gobject group near: найдено {} групп на map {} в радиусе {:.1f}.",
                snap.Items.size(), snap.MapId, radius).c_str());
            return Ok(handler, "near", fmt::format("{}", snap.Items.size()));
        }

        static bool HandleCheck(ChatHandler* handler, GameObjectSpawnId groupGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "check"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "check"))
                return false;

            ActivateGroup(player, root);
            std::string const report = sGobGroupMgr.BuildCheck(root);
            SendReport(handler, report);
            if (report.rfind("OK ", 0) != 0)
            {
                GobGroupProtocol::SendResult(player, "check", "error", report);
                handler->SetSentErrorMessage(true);
                return false;
            }
            return Ok(handler, "check", report);
        }

        static bool HandleStatus(ChatHandler* handler, GameObjectSpawnId groupGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "status"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "status"))
                return false;

            ActivateGroup(player, root);
            SendReport(handler, sGobGroupMgr.BuildStatus(root));

            GobGroupStatusSnapshot snap;
            std::string error;
            if (!sGobGroupMgr.TryGetStatusSnapshot(root, snap, error))
                return Fail(handler, "status", error);

            GobGroupProtocol::SendStatus(player, snap);
            return Ok(handler, "status", fmt::format("{}", root));
        }

        static bool HandleCapture(ChatHandler* handler, GameObjectSpawnId guid, Optional<EXACT_SEQUENCE("silent")> silentFlag)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "capture"))
                return false;

            bool silent = !!silentFlag;
            ObjectGuid::LowType anyGuid = *guid;
            bool const allowed = silent
                ? RoleplayCommandPhaseGuard::AllowsAuthorizedGobMoverMutation(player,
                    RoleplayPhaseSpawnType::GameObject, anyGuid, "gobgroup_capture")
                : RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, handler,
                    RoleplayPhaseSpawnType::GameObject, anyGuid, false, "gobgroup_capture");
            if (!allowed)
            {
                std::string const error = fmt::format(
                    "GO {} capture requires current logical RP phase and editor/manager/owner role.", anyGuid);
                if (silent)
                {
                    GobGroupProtocol::SendResult(player, "capture", "error", error);
                    return true;
                }
                return Fail(handler, "capture", error);
            }

            std::string error;
            if (!sGobGroupMgr.Capture(anyGuid, silent, error))
            {
                if (silent)
                {
                    GobGroupProtocol::SendResult(player, "capture", "error", error);
                    return true;
                }
                return Fail(handler, "capture", error);
            }

            if (ObjectGuid::LowType root = sGobGroupMgr.FindRootGuid(anyGuid))
                ActivateGroup(player, root);

            if (!silent)
                handler->SendSysMessage(fmt::format(".gobject group capture: {} обновлён.", anyGuid).c_str());
            return Ok(handler, "capture", fmt::format("{}", anyGuid));
        }

        static bool HandleRecalc(ChatHandler* handler, GameObjectSpawnId groupGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "recalc"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "recalc", false, true, "gobgroup_recalc"))
                return false;

            std::string error;
            if (!sGobGroupMgr.Recalc(root, error))
                return Fail(handler, "recalc", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group recalc: relative transforms пересчитаны.");
            return Ok(handler, "recalc", fmt::format("{}", root));
        }

        static bool HandleSync(ChatHandler* handler, GameObjectSpawnId groupGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "sync"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "sync", false, true, "gobgroup_sync"))
                return false;

            std::string error;
            if (!sGobGroupMgr.Sync(root, error))
                return Fail(handler, "sync", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group sync: задача поставлена.");
            return Queued(handler, "sync", root, fmt::format("{}", root));
        }

        static bool HandleMove(ChatHandler* handler, GameObjectSpawnId groupGuid, Optional<std::array<float, 3>> xyz,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "move"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_move", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "move", bypass, true, "gobgroup_move"))
                return false;

            Position pos = player->GetPosition();
            if (xyz)
                pos.Relocate((*xyz)[0], (*xyz)[1], (*xyz)[2], pos.GetOrientation());

            std::string error;
            if (!sGobGroupMgr.Move(root, pos, error))
                return Fail(handler, "move", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group move: задача поставлена.");
            return Queued(handler, "move", root, fmt::format("{}", root));
        }

        static bool HandleTurn(ChatHandler* handler, GameObjectSpawnId groupGuid, Optional<float> orientation,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "turn"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_turn", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "turn", bypass, true, "gobgroup_turn"))
                return false;

            float o = orientation.value_or(player->GetOrientation());
            std::string error;
            if (!sGobGroupMgr.Turn(root, o, error))
                return Fail(handler, "turn", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group turn: задача поставлена.");
            return Queued(handler, "turn", root, fmt::format("{}", root));
        }

        static bool ResolveNudgeDelta(Player* player, std::string_view a, float b, Optional<float> c,
            float& dx, float& dy, float& dz, std::string& error)
        {
            if (c.has_value())
            {
                try
                {
                    dx = std::stof(std::string(a));
                }
                catch (...)
                {
                    error = "nudge xyz form: <dx> <dy> <dz>";
                    return false;
                }
                dy = b;
                dz = *c;
                return true;
            }

            std::string dir;
            dir.reserve(a.size());
            for (char ch : a)
                dir.push_back(char(std::toupper(static_cast<unsigned char>(ch))));

            float const step = b;
            dx = dy = dz = 0.f;
            if (dir == "UP")
            {
                dz = step;
                return true;
            }
            if (dir == "DOWN")
            {
                dz = -step;
                return true;
            }

            // DIR_XY: forward=(+1,0) … left=(0,+1); rotate by player facing.
            float fx = 0.f;
            float fy = 0.f;
            if (dir == "F") { fx = 1.f; fy = 0.f; }
            else if (dir == "FR") { fx = 1.f; fy = -1.f; }
            else if (dir == "R") { fx = 0.f; fy = -1.f; }
            else if (dir == "BR") { fx = -1.f; fy = -1.f; }
            else if (dir == "B") { fx = -1.f; fy = 0.f; }
            else if (dir == "BL") { fx = -1.f; fy = 1.f; }
            else if (dir == "L") { fx = 0.f; fy = 1.f; }
            else if (dir == "FL") { fx = 1.f; fy = 1.f; }
            else
            {
                error = "nudge dir: F|FR|R|BR|B|BL|L|FL|UP|DOWN";
                return false;
            }

            float const len = std::sqrt(fx * fx + fy * fy);
            if (len > 0.f)
            {
                fx /= len;
                fy /= len;
            }

            float const o = player->GetOrientation();
            float const cosO = std::cos(o);
            float const sinO = std::sin(o);
            dx = (fx * cosO - fy * sinO) * step;
            dy = (fx * sinO + fy * cosO) * step;
            return true;
        }

        static bool HandleNudge(ChatHandler* handler, GameObjectSpawnId groupGuid, std::string_view a, float b,
            Optional<float> c, Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "nudge"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_nudge", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "nudge", bypass, true, "gobgroup_nudge"))
                return false;

            float dx = 0.f, dy = 0.f, dz = 0.f;
            std::string error;
            if (!ResolveNudgeDelta(player, a, b, c, dx, dy, dz, error))
                return Fail(handler, "nudge", error);

            if (!sGobGroupMgr.Nudge(root, dx, dy, dz, error))
                return Fail(handler, "nudge", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group nudge: задача поставлена.");
            return Queued(handler, "nudge", root, fmt::format("{}", root));
        }

        static bool HandleRotate(ChatHandler* handler, GameObjectSpawnId groupGuid, float dYawDeg,
            Optional<float> dPitchDeg, Optional<float> dRollDeg,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "rotate"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_rotate", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "rotate", bypass, true, "gobgroup_rotate"))
                return false;

            float const dYaw = float(dYawDeg * float(M_PI) / 180.f);
            float const dPitch = float(dPitchDeg.value_or(0.f) * float(M_PI) / 180.f);
            float const dRoll = float(dRollDeg.value_or(0.f) * float(M_PI) / 180.f);

            std::string error;
            if (!sGobGroupMgr.RotateDelta(root, dYaw, dPitch, dRoll, error))
                return Fail(handler, "rotate", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group rotate: задача поставлена.");
            return Queued(handler, "rotate", root, fmt::format("{}", root));
        }

        static bool HandleScale(ChatHandler* handler, GameObjectSpawnId groupGuid, float factor,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "scale"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_scale", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "scale", bypass, true, "gobgroup_scale"))
                return false;

            std::string error;
            if (!sGobGroupMgr.ScaleUniform(root, factor, error))
                return Fail(handler, "scale", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group scale: offsets/size обновлены, transform поставлен.");
            return Queued(handler, "scale", root, fmt::format("{}", root));
        }

        static bool HandleRelocate(ChatHandler* handler, GameObjectSpawnId groupGuid, uint32 mapId,
            float x, float y, float z, float o, EXACT_SEQUENCE("confirm"), Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "relocate"))
                return false;

            ObjectGuid::LowType root = 0;
            bool bypass = false;
            if (!ResolveRequestedBypass(player, handler, allPhases.has_value(), "gobgroup_relocate", bypass))
                return false;
            if (!ResolveGroupRoot(handler, player, *groupGuid, root, "relocate", bypass, true, "gobgroup_relocate"))
                return false;

            Position pos;
            pos.Relocate(x, y, z, o);

            std::string error;
            if (!sGobGroupMgr.Relocate(root, mapId, pos, true, error))
                return Fail(handler, "relocate", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group relocate: задача поставлена.");
            return Queued(handler, "relocate", root, fmt::format("{}:{}", root, mapId));
        }

        static bool HandleReload(ChatHandler* handler)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "reload"))
                return false;

            std::string error;
            if (!sGobGroupMgr.Reload(error))
                return Fail(handler, "reload", error);

            handler->SendSysMessage(".gobject group reload: cache перезагружен.");
            return Ok(handler, "reload");
        }

        static bool HandleCleanup(ChatHandler* handler, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "cleanup"))
                return false;

            std::string report;
            if (!sGobGroupMgr.CleanupOrphans(true, report))
                return Fail(handler, "cleanup", report);

            SendReport(handler, report);
            return Ok(handler, "cleanup");
        }
    };
}

void AddSC_NobleNextGobGroupCommands()
{
    new RoleplayCore::NobleNext::GobGroupCommands();
}
