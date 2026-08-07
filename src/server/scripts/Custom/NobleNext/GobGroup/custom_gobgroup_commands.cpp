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
#include "WorldSession.h"

#include <cmath>
#include <fmt/format.h>
#include <limits>
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

        static bool ResolveGroupRoot(ChatHandler* handler, ObjectGuid::LowType anyGuid, ObjectGuid::LowType& outRoot,
            std::string_view verb)
        {
            outRoot = sGobGroupMgr.FindRootGuid(anyGuid);
            if (!outRoot)
            {
                std::string const detail = fmt::format("GO {} не состоит в группе.", anyGuid);
                handler->SendSysMessage(detail.c_str());
                GobGroupProtocol::SendResult(PlayerOf(handler), verb, "error", detail);
                handler->SetSentErrorMessage(true);
                return false;
            }
            return true;
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
            handler->SendSysMessage(".gobject group capabilities: NN_GOBGROUP CAPS отправлен.");
            return Ok(handler, "capabilities");
        }

        static bool HandleCreate(ChatHandler* handler, GameObjectSpawnId rootSpawnId, Optional<Tail> name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "create"))
                return false;

            ObjectGuid::LowType const rootGuid = *rootSpawnId;
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
            if (!ResolveGroupRoot(handler, *guid, root, "use"))
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
            if (!ResolveGroupRoot(handler, *guid, root, "target"))
                return false;

            ActivateGroup(player, root);
            RememberTargetedGo(player, root);
            handler->SendSysMessage(fmt::format("Активная группа: root {}.", root).c_str());
            return Ok(handler, "target", fmt::format("{}", root));
        }

        static bool HandleAdd(ChatHandler* handler, GameObjectSpawnId groupGuid, GameObjectSpawnId memberGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "add"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "add"))
                return false;

            ObjectGuid::LowType const member = *memberGuid;
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
            if (!ResolveGroupRoot(handler, *groupGuid, root, "scan"))
                return false;

            std::vector<ObjectGuid::LowType> candidates;
            std::string report;
            uint32 count = sGobGroupMgr.ScanNear(player, root, radius, candidates, report);
            SendReport(handler, report);
            handler->SendSysMessage(fmt::format(".gobject group scan: кандидатов {}.", count).c_str());
            return Ok(handler, "scan", fmt::format("{}", count));
        }

        static bool HandleAddNear(ChatHandler* handler, GameObjectSpawnId groupGuid, float radius, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "addnear"))
                return false;

            if (!ValidateRadius(handler, player, "addnear", radius))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "addnear"))
                return false;

            std::string report;
            if (!sGobGroupMgr.AddNear(player, root, radius, true, report))
                return Fail(handler, "addnear", report);

            ActivateGroup(player, root);
            SendReport(handler, report);
            return Ok(handler, "addnear", fmt::format("{}", root));
        }

        static bool HandleRemove(ChatHandler* handler, GameObjectSpawnId groupGuid, GameObjectSpawnId memberGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "remove"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "remove"))
                return false;

            ObjectGuid::LowType const member = *memberGuid;
            std::string error;
            if (!sGobGroupMgr.RemoveMember(root, member, error))
                return Fail(handler, "remove", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(fmt::format("Member {} удалён из группы root {} (GO остаётся).", member, root).c_str());
            return Ok(handler, "remove", fmt::format("{}:{}", root, member));
        }

        static bool HandleDissolve(ChatHandler* handler, GameObjectSpawnId groupGuid)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "dissolve"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "dissolve"))
                return false;

            std::string error;
            if (!sGobGroupMgr.Dissolve(root, error))
                return Fail(handler, "dissolve", error);

            if (sGobGroupMgr.GetActiveRoot(player->GetSession()->GetAccountId()) == root)
                ActivateGroup(player, 0);
            handler->SendSysMessage(fmt::format("Группа root {} распущена (GO остаются).", root).c_str());
            return Ok(handler, "dissolve", fmt::format("{}", root));
        }

        static bool HandleDelete(ChatHandler* handler, GameObjectSpawnId groupGuid, EXACT_SEQUENCE("full-force"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "delete"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "delete"))
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
            if (ObjectGuid::LowType const root = sGobGroupMgr.FindRootGuid(guid))
                ActivateGroup(player, root);

            SendReport(handler, sGobGroupMgr.BuildInfo(guid));

            GobGroupInfoSnapshot snap;
            std::string error;
            if (!sGobGroupMgr.TryGetInfoSnapshot(guid, snap, error))
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

            SendReport(handler, sGobGroupMgr.BuildList(mapId));

            GobGroupListSnapshot snap;
            sGobGroupMgr.GetListSnapshot(mapId, snap);
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
            if (!ResolveGroupRoot(handler, *groupGuid, root, "check"))
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
            if (!ResolveGroupRoot(handler, *groupGuid, root, "status"))
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
            if (!ResolveGroupRoot(handler, *groupGuid, root, "recalc"))
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
            if (!ResolveGroupRoot(handler, *groupGuid, root, "sync"))
                return false;

            std::string error;
            if (!sGobGroupMgr.Sync(root, error))
                return Fail(handler, "sync", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group sync: задача поставлена.");
            return Queued(handler, "sync", root, fmt::format("{}", root));
        }

        static bool HandleMove(ChatHandler* handler, GameObjectSpawnId groupGuid, Optional<std::array<float, 3>> xyz)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "move"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "move"))
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

        static bool HandleTurn(ChatHandler* handler, GameObjectSpawnId groupGuid, Optional<float> orientation)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "turn"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "turn"))
                return false;

            float o = orientation.value_or(player->GetOrientation());
            std::string error;
            if (!sGobGroupMgr.Turn(root, o, error))
                return Fail(handler, "turn", error);

            ActivateGroup(player, root);
            handler->SendSysMessage(".gobject group turn: задача поставлена.");
            return Queued(handler, "turn", root, fmt::format("{}", root));
        }

        static bool HandleRelocate(ChatHandler* handler, GameObjectSpawnId groupGuid, uint32 mapId,
            float x, float y, float z, float o, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player, "relocate"))
                return false;

            ObjectGuid::LowType root = 0;
            if (!ResolveGroupRoot(handler, *groupGuid, root, "relocate"))
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
