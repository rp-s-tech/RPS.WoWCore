/*
 * NobleNext — .rps phase commands for logical RP world-map contexts.
 */

#include "RoleplayPhaseMgr.h"
#include "noble_next_gobgroup_mgr.h"

#include "CharacterCache.h"
#include "AccountMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "StringConvert.h"
#include "WorldSession.h"

#include <algorithm>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <vector>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class RoleplayPhaseCommands : public CommandScript
    {
    public:
        RoleplayPhaseCommands() : CommandScript("noble_next_rpphase_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable spawnTable =
            {
                { "info",   HandleSpawnInfo,   LANG_COMMAND_RP_PHASE_SPAWN_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_SPAWN_INFO,   Console::No },
                { "assign", HandleSpawnAssign, LANG_COMMAND_RP_PHASE_SPAWN_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_SPAWN_ASSIGN, Console::No },
                { "clear",  HandleSpawnClear,  LANG_COMMAND_RP_PHASE_SPAWN_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_SPAWN_CLEAR,  Console::No },
            };
            static ChatCommandTable setTable =
            {
                { "name",        HandleSetName,       LANG_COMMAND_RP_PHASE_SET_NAME_HELP,        rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_NAME,        Console::No },
                { "public",      HandleSetPublic,     LANG_COMMAND_RP_PHASE_SET_PUBLIC_HELP,      rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_PUBLIC,      Console::No },
                { "owner",       HandleSetOwner,      LANG_COMMAND_RP_PHASE_SET_OWNER_HELP,       rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_OWNER,       Console::No },
                { "role",        HandleSetRole,       LANG_COMMAND_RP_PHASE_SET_ROLE_HELP,        rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_ROLE,        Console::No },
                { "spawn",       HandleSetSpawn,      LANG_COMMAND_RP_PHASE_SET_SPAWN_HELP,       rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_SPAWN,       Console::No },
                { "enter-spawn", HandleSetEnterSpawn, LANG_COMMAND_RP_PHASE_SET_ENTER_SPAWN_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_ENTER_SPAWN, Console::No },
            };

            // Short GM path `.rps pha set owner <id> <character>` without force token.
            static ChatCommandTable phaSetTable =
            {
                { "owner", HandleSetOwnerShort, LANG_COMMAND_RP_PHASE_SET_OWNER_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_SET_OWNER, Console::No },
            };
            static ChatCommandTable phaTable =
            {
                { "set", phaSetTable },
            };

            static ChatCommandTable phaseTable =
            {
                { "help",    HandleHelp,    LANG_COMMAND_RP_PHASE_HELP,    rbac::RBAC_PERM_COMMAND_RP_PHASE_HELP,    Console::No },
                { "create",  HandleCreate,  LANG_COMMAND_RP_PHASE_CREATE_HELP,  rbac::RBAC_PERM_COMMAND_RP_PHASE_CREATE,  Console::No },
                { "list",    HandleList,    LANG_COMMAND_RP_PHASE_LIST_HELP,    rbac::RBAC_PERM_COMMAND_RP_PHASE_LIST,    Console::No },
                { "info",    HandleInfo,    LANG_COMMAND_RP_PHASE_INFO_HELP,    rbac::RBAC_PERM_COMMAND_RP_PHASE_INFO,    Console::No },
                { "enter",   HandleEnter,   LANG_COMMAND_RP_PHASE_ENTER_HELP,   rbac::RBAC_PERM_COMMAND_RP_PHASE_ENTER,   Console::No },
                { "leave",   HandleLeave,   LANG_COMMAND_RP_PHASE_LEAVE_HELP,   rbac::RBAC_PERM_COMMAND_RP_PHASE_LEAVE,   Console::No },
                { "invite",  HandleInvite,  LANG_COMMAND_RP_PHASE_INVITE_HELP,  rbac::RBAC_PERM_COMMAND_RP_PHASE_INVITE,  Console::No },
                { "revoke",  HandleRevoke,  LANG_COMMAND_RP_PHASE_REVOKE_HELP,  rbac::RBAC_PERM_COMMAND_RP_PHASE_REVOKE,  Console::No },
                { "archive",   HandleArchive,   LANG_COMMAND_RP_PHASE_ARCHIVE_HELP,   rbac::RBAC_PERM_COMMAND_RP_PHASE_ARCHIVE,   Console::No },
                { "unarchive", HandleUnarchive, LANG_COMMAND_RP_PHASE_UNARCHIVE_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_ARCHIVE,   Console::No },
                { "reload",    HandleReload,    LANG_COMMAND_RP_PHASE_RELOAD_HELP,    rbac::RBAC_PERM_COMMAND_RP_PHASE_RELOAD,    Console::No },
                { "spawn",   spawnTable },
                { "set",     setTable },
                { "goto",    HandleGoto, LANG_COMMAND_RP_PHASE_GOTO_HELP, rbac::RBAC_PERM_COMMAND_RP_PHASE_GOTO, Console::No },
            };

            // Canonical root is .rps (Role Play Systems). `pha` = short alias for set owner.
            static ChatCommandTable rpsTable =
            {
                { "phase", phaseTable },
                { "pha",   phaTable },
            };

            static ChatCommandTable commandTable =
            {
                { "rps", rpsTable },
            };
            return commandTable;
        }

    private:
        static constexpr std::string_view CommandAuditDetail = R"({"source":"command"})";
        static constexpr std::string_view AllPhasesAuditDetail = R"({"source":"command","bypass":"all_phases"})";

        static bool RequirePlayer(ChatHandler* handler, Player*& player)
        {
            player = handler ? handler->GetPlayer() : nullptr;
            if (player)
                return true;

            if (handler)
            {
                handler->SendSysMessage("Эта команда доступна только в игре.");
                handler->SetSentErrorMessage(true);
            }
            return false;
        }

        static bool Fail(ChatHandler* handler, std::string_view message)
        {
            handler->SendSysMessage(std::string(message).c_str());
            handler->SetSentErrorMessage(true);
            return false;
        }

        static uint64 CharacterGuid(Player const* player)
        {
            return player ? player->GetGUID().GetCounter() : 0;
        }

        static uint32 AccountId(Player const* player)
        {
            return player && player->GetSession() ? player->GetSession()->GetAccountId() : 0;
        }

        static bool StaffAccess(Player const* player)
        {
            return player && player->GetSession()
                && player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
        }

        static bool ServerStaff(Player const* player)
        {
            return player && player->GetSession()
                && sRoleplayPhaseMgr.CanMutateCommonWorld(player->GetSession()->GetSecurity());
        }

        static void Audit(Player const* player, std::string_view action, uint64 phaseId, std::string_view detail = CommandAuditDetail)
        {
            sRoleplayPhaseMgr.WriteAudit(CharacterGuid(player), AccountId(player), action, phaseId, detail);
        }

        static bool UseAllPhasesBypass(ChatHandler* handler, Player* player, bool requested, std::string_view action)
        {
            if (!requested)
                return false;

            if (!player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES))
                return Fail(handler, "Для --all-phases требуется отдельное право доступа.");

            Audit(player, action, 0, AllPhasesAuditDetail);
            return true;
        }

        static bool RequireRole(ChatHandler* handler, Player* player, uint64 phaseId, RoleplayPhaseRole role)
        {
            if (sRoleplayPhaseMgr.HasRole(phaseId, CharacterGuid(player), AccountId(player), role))
                return true;

            return Fail(handler, "Недостаточно прав в этой RP phase или phase недоступна.");
        }

        static std::string_view TrimLeading(std::string_view value)
        {
            size_t const first = value.find_first_not_of(" \t");
            return first == std::string_view::npos ? std::string_view() : value.substr(first);
        }

        static bool HasScopeFlag(std::string_view value, std::string_view flag)
        {
            return value == flag || (value.starts_with(flag) && value.size() > flag.size()
                && (value[flag.size()] == ' ' || value[flag.size()] == '\t'));
        }

        static bool ParseCreateScope(ChatHandler* handler, Optional<Tail> const& arguments,
            Optional<uint32>& mapId, std::string& description)
        {
            mapId.reset(); // Global phase is the default.
            description.clear();
            if (!arguments)
                return true;

            std::string_view tail = TrimLeading(*arguments);
            if (tail.empty())
                return true;

            if (HasScopeFlag(tail, "--global"))
            {
                tail.remove_prefix(std::string_view("--global").size());
                description = std::string(TrimLeading(tail));
                return true;
            }

            if (HasScopeFlag(tail, "--map"))
            {
                tail.remove_prefix(std::string_view("--map").size());
                tail = TrimLeading(tail);
                size_t const separator = tail.find_first_of(" \t");
                std::string_view const mapToken = tail.substr(0, separator);
                Optional<uint32> const parsedMapId = Trinity::StringTo<uint32>(mapToken);
                if (!parsedMapId)
                    return Fail(handler, "После --map требуется корректный map-id.");

                mapId = *parsedMapId;
                if (separator != std::string_view::npos)
                    description = std::string(TrimLeading(tail.substr(separator)));
                return true;
            }

            // Without a scope flag, the full tail describes a global phase.
            description = std::string(tail);
            return true;
        }

        static Optional<RoleplayPhaseRole> ParseMemberRole(std::string_view role)
        {
            if (role == "viewer")
                return RoleplayPhaseRole::Viewer;
            if (role == "editor" || role == "builder")
                return RoleplayPhaseRole::Editor;
            if (role == "manager")
                return RoleplayPhaseRole::Manager;
            return {};
        }

        static Optional<RoleplayPhaseSpawnType> ParseSpawnType(std::string_view type)
        {
            if (type == "creature")
                return RoleplayPhaseSpawnType::Creature;
            if (type == "gameobject")
                return RoleplayPhaseSpawnType::GameObject;
            return {};
        }

        static std::string_view RoleName(RoleplayPhaseRole role)
        {
            switch (role)
            {
                case RoleplayPhaseRole::Viewer: return "viewer";
                case RoleplayPhaseRole::Editor: return "editor";
                case RoleplayPhaseRole::Manager: return "manager";
                case RoleplayPhaseRole::Owner: return "owner";
                default: return "none";
            }
        }

        static std::string_view SpawnTypeName(RoleplayPhaseSpawnType type)
        {
            return type == RoleplayPhaseSpawnType::Creature ? "creature" : "gameobject";
        }

        static bool HandleHelp(ChatHandler* handler)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            handler->SendSysMessage(LANG_COMMAND_RP_PHASE_HELP);
            handler->SendSysMessage(LANG_COMMAND_RP_PHASE_SET_HELP);
            handler->SendSysMessage(LANG_COMMAND_RP_PHASE_GOTO_HELP);
            return true;
        }

        static bool HandleCreate(ChatHandler* handler, QuotedString name, Optional<Tail> arguments)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<uint32> mapId;
            std::string description;
            if (!ParseCreateScope(handler, arguments, mapId, description))
                return false;

            uint64 phaseId = 0;
            if (!sRoleplayPhaseMgr.Create(std::string(name), description, AccountId(player),
                CharacterGuid(player), phaseId, mapId))
                return Fail(handler, "Не удалось создать RP phase. Проверьте имя, map-id и доступность базы данных.");

            Audit(player, "command_create", phaseId);
            handler->SendSysMessage(fmt::format("RP phase {} создана: {}.", phaseId, std::string(name)).c_str());
            return true;
        }

        static bool HandleList(ChatHandler* handler, Optional<EXACT_SEQUENCE("my")> mine,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            if (mine && allPhases)
                return Fail(handler, "Используйте либо my, либо --all-phases.");

            // RBAC 3045 grants automatic staff read of every phase; --all-phases remains an
            // explicit audited alias for the same view policy.
            bool const staffAccess = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
            bool const showAll = !mine && (staffAccess || UseAllPhasesBypass(handler, player, !!allPhases, "command_list_all"));
            if (allPhases && !showAll && !staffAccess)
                return false;

            std::vector<RoleplayPhaseInfo> phases;
            sRoleplayPhaseMgr.GetPhaseList(phases);
            std::sort(phases.begin(), phases.end(), [](RoleplayPhaseInfo const& left, RoleplayPhaseInfo const& right)
            {
                return left.Id < right.Id;
            });

            uint32 shown = 0;
            for (RoleplayPhaseInfo const& phase : phases)
            {
                if (mine)
                {
                    if (!sRoleplayPhaseMgr.IsOwnedOrMember(phase.Id, CharacterGuid(player), AccountId(player)))
                        continue;
                }
                else if (!sRoleplayPhaseMgr.CanDiscover(phase.Id, CharacterGuid(player), AccountId(player), showAll))
                    continue;

                std::string const map = phase.MapId ? fmt::format("{}", *phase.MapId) : "global";
                std::string const state = phase.Archived ? "archived"
                    : (phase.Valid && phase.Enabled ? "active" : "unavailable");
                std::string owner = "Server";
                if (phase.OwnerAccountId && !AccountMgr::GetName(phase.OwnerAccountId, owner))
                    owner = "Unknown";
                handler->SendSysMessage(fmt::format("[{}] {} (map {}, {}, {}, owner {}, spawn {}, members {})",
                    phase.Id, phase.Name, map, state, phase.IsPublic ? "public" : "private", owner,
                    phase.HasSpawn() ? (phase.EnterSpawn ? "set/enabled" : "set/disabled") : "not set",
                    phase.Members.size()).c_str());
                ++shown;
            }

            handler->SendSysMessage(fmt::format("RP phases{}: {}.", mine ? " (my)" : "", shown).c_str());
            return true;
        }

        static bool HandleInfo(ChatHandler* handler, uint64 phaseId, Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            bool const staffAccess = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES);
            bool const bypass = staffAccess || UseAllPhasesBypass(handler, player, !!allPhases, "command_info_all");
            if (allPhases && !bypass && !staffAccess)
                return false;
            if (!sRoleplayPhaseMgr.CanDiscover(phaseId, CharacterGuid(player), AccountId(player), bypass))
                return Fail(handler, "Недостаточно прав в этой RP phase или phase недоступна.");

            RoleplayPhaseInfo phase;
            if (!sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase))
                return Fail(handler, "RP phase не найдена.");

            std::sort(phase.Members.begin(), phase.Members.end(), [](RoleplayPhaseMemberInfo const& left, RoleplayPhaseMemberInfo const& right)
            {
                return left.CharacterGuid < right.CharacterGuid;
            });
            handler->SendSysMessage(fmt::format("RP phase {}: {}.", phase.Id, phase.Name).c_str());
            std::string owner = "Server";
            if (phase.OwnerAccountId && !AccountMgr::GetName(phase.OwnerAccountId, owner))
                owner = "Unknown";
            handler->SendSysMessage(fmt::format("  owner: {} (account {}), map: {}, public: {}, enabled: {}, archived: {}, valid: {}.",
                owner, phase.OwnerAccountId, phase.MapId ? fmt::format("{}", *phase.MapId) : "global",
                phase.IsPublic ? "yes" : "no", phase.Enabled ? "yes" : "no", phase.Archived ? "yes" : "no", phase.Valid ? "yes" : "no").c_str());
            if (phase.HasSpawn())
                handler->SendSysMessage(fmt::format("  enter spawn: map {} ({:.3f}, {:.3f}, {:.3f}, {:.3f}) {}.",
                    *phase.SpawnMap, *phase.SpawnX, *phase.SpawnY, *phase.SpawnZ, *phase.SpawnO,
                    phase.EnterSpawn ? "enabled" : "disabled").c_str());
            else
                handler->SendSysMessage("  enter spawn: not set.");
            handler->SendSysMessage(fmt::format("  description: {}", phase.Description.empty() ? "-" : phase.Description).c_str());
            for (RoleplayPhaseMemberInfo const& member : phase.Members)
                handler->SendSysMessage(fmt::format("  member {}: {}.", member.CharacterGuid, RoleName(member.Role)).c_str());
            return true;
        }

        static bool HandleEnter(ChatHandler* handler, uint64 phaseId)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            RoleplayPhaseInfo phase;
            if (!sRoleplayPhaseMgr.GetPhaseInfo(phaseId, phase))
                return Fail(handler, "RP phase не найдена.");
            if (!(phase.EnterSpawn && phase.HasSpawn()
                ? sRoleplayPhaseMgr.GotoPhase(player, phaseId)
                : sRoleplayPhaseMgr.TransitionPlayer(player, phaseId)))
                return Fail(handler, "Вход в RP phase отклонён: проверьте ACL, archive и map scope.");

            Audit(player, "command_enter", phaseId);
            handler->SendSysMessage(fmt::format("Активная RP phase: {}.", phaseId).c_str());
            return true;
        }

        static bool HandleLeave(ChatHandler* handler)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            uint64 const previousPhaseId = sRoleplayPhaseMgr.GetPlayerPhaseId(CharacterGuid(player));
            if (!sRoleplayPhaseMgr.TransitionPlayer(player, 0))
                return Fail(handler, "Не удалось выйти из RP phase.");

            Audit(player, "command_leave", previousPhaseId);
            handler->SendSysMessage("Активная RP phase сброшена в common (0).");
            return true;
        }

        static bool HandleInvite(ChatHandler* handler, uint64 phaseId, PlayerIdentifier target, std::string role)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<RoleplayPhaseRole> memberRole = ParseMemberRole(role);
            uint64 const targetGuid = target.GetGUID().GetCounter();
            if (!memberRole || !targetGuid)
                return Fail(handler, "Роль должна быть viewer, editor или manager; игрок должен существовать.");

            if (!sRoleplayPhaseMgr.SetMemberRole(phaseId, targetGuid, *memberRole, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось выдать ACL: требуется manager role в доступной RP phase.");

            Audit(player, "command_invite", phaseId);
            handler->SendSysMessage(fmt::format("Игрок {} ({}) получил роль {} в RP phase {}.",
                target.GetName(), targetGuid, RoleName(*memberRole), phaseId).c_str());
            return true;
        }

        static bool HandleRevoke(ChatHandler* handler, uint64 phaseId, PlayerIdentifier target, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            uint64 const targetGuid = target.GetGUID().GetCounter();
            if (!targetGuid)
                return Fail(handler, "Игрок не найден.");

            if (!sRoleplayPhaseMgr.RemoveMember(phaseId, targetGuid, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось отозвать ACL: требуется manager role в доступной RP phase.");

            Audit(player, "command_revoke", phaseId);
            handler->SendSysMessage(fmt::format("ACL игрока {} ({}) отозван в RP phase {}.", target.GetName(), targetGuid, phaseId).c_str());
            return true;
        }

        static bool HandleArchive(ChatHandler* handler, uint64 phaseId, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            bool const staffBypass = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES)
                && !sRoleplayPhaseMgr.CanArchive(phaseId, CharacterGuid(player), AccountId(player), false, ServerStaff(player));
            if (!sRoleplayPhaseMgr.Archive(phaseId, CharacterGuid(player), AccountId(player), staffBypass, ServerStaff(player)))
                return Fail(handler, "Не удалось архивировать RP phase: требуется Owner или RBAC 3045.");

            Audit(player, staffBypass ? "command_archive_staff" : "command_archive", phaseId,
                staffBypass ? AllPhasesAuditDetail : CommandAuditDetail);
            handler->SendSysMessage(fmt::format("RP phase {} архивирована (soft archive).", phaseId).c_str());
            return true;
        }

        static bool HandleUnarchive(ChatHandler* handler, uint64 phaseId, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            bool const staffBypass = player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES)
                && !sRoleplayPhaseMgr.CanUnarchive(phaseId, CharacterGuid(player), AccountId(player), false, ServerStaff(player));
            if (!sRoleplayPhaseMgr.Unarchive(phaseId, CharacterGuid(player), AccountId(player), staffBypass, ServerStaff(player)))
                return Fail(handler, "Не удалось разархивировать RP phase: требуется manager/owner или RBAC 3045.");

            Audit(player, staffBypass ? "command_unarchive_staff" : "command_unarchive", phaseId,
                staffBypass ? AllPhasesAuditDetail : CommandAuditDetail);
            handler->SendSysMessage(fmt::format("RP phase {} разархивирована.", phaseId).c_str());
            return true;
        }

        static bool HandleSetName(ChatHandler* handler, uint64 phaseId, QuotedString name)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            if (!sRoleplayPhaseMgr.Rename(phaseId, std::string(name), CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось переименовать: требуется Owner (или staff); имя непустое.");

            Audit(player, "command_set_name", phaseId);
            handler->SendSysMessage(fmt::format("RP phase {} переименована: {}.", phaseId, std::string(name)).c_str());
            return true;
        }

        static bool HandleSetPublic(ChatHandler* handler, uint64 phaseId, std::string value)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<bool> isPublic;
            if (value == "true" || value == "1")
                isPublic = true;
            else if (value == "false" || value == "0")
                isPublic = false;
            if (!isPublic)
                return Fail(handler, "Значение public: true или false.");

            if (!sRoleplayPhaseMgr.SetPublic(phaseId, *isPublic, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось изменить public: требуется Owner (или staff); Server phase требует GM1+.");

            Audit(player, "command_set_public", phaseId);
            handler->SendSysMessage(fmt::format("RP phase {} теперь {}.", phaseId, *isPublic ? "public" : "private").c_str());
            return true;
        }

        static bool ApplySetOwner(ChatHandler* handler, Player* player, uint64 phaseId, std::string const& target)
        {
            uint32 ownerAccountId = 0;
            uint64 managerGuid = 0;
            if (target != "0")
            {
                CharacterCacheEntry const* character = sCharacterCache->GetCharacterCacheByName(target);
                if (!character || !character->AccountId || character->IsDeleted)
                    return Fail(handler, "Персонаж нового owner не найден.");
                ownerAccountId = character->AccountId;
                managerGuid = character->Guid.GetCounter();
            }

            if (!sRoleplayPhaseMgr.SetOwner(phaseId, ownerAccountId, managerGuid, CharacterGuid(player), AccountId(player),
                true, StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось сменить owner: требуется Owner (или staff); Server phase требует GM1+.");

            Audit(player, "command_set_owner", phaseId);
            handler->SendSysMessage(fmt::format("Owner RP phase {}: {}.", phaseId, ownerAccountId ? target : "Server").c_str());
            return true;
        }

        // Canonical: `.rps phase set owner <id> <character|0> force`
        static bool HandleSetOwner(ChatHandler* handler, uint64 phaseId, std::string target, EXACT_SEQUENCE("force"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;
            return ApplySetOwner(handler, player, phaseId, target);
        }

        // Short GM: `.rps pha set owner <id> <character|0>` (no force token)
        static bool HandleSetOwnerShort(ChatHandler* handler, uint64 phaseId, std::string target)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;
            return ApplySetOwner(handler, player, phaseId, target);
        }

        static bool HandleSetRole(ChatHandler* handler, uint64 phaseId, PlayerIdentifier target, std::string role)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<RoleplayPhaseRole> memberRole = ParseMemberRole(role);
            uint64 const targetGuid = target.GetGUID().GetCounter();
            if (!memberRole || !targetGuid)
                return Fail(handler, "Роль должна быть viewer, editor или manager; персонаж должен существовать.");

            if (!sRoleplayPhaseMgr.SetMemberRole(phaseId, targetGuid, *memberRole, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось изменить роль: Manager выдаёт только viewer/editor; manager — Owner/staff.");

            Audit(player, "command_set_role", phaseId);
            handler->SendSysMessage(fmt::format("Роль {} для {} установлена в RP phase {}.",
                RoleName(*memberRole), target.GetName(), phaseId).c_str());
            return true;
        }

        static bool HandleSetSpawn(ChatHandler* handler, uint64 phaseId)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            if (!sRoleplayPhaseMgr.SetEnterSpawnPoint(phaseId, player->GetMapId(), player->GetPositionX(),
                player->GetPositionY(), player->GetPositionZ(), player->GetOrientation(), CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось сохранить enter spawn: map scope и права manager/owner обязательны.");

            Audit(player, "command_set_spawn", phaseId);
            handler->SendSysMessage(fmt::format("Enter spawn RP phase {} сохранён.", phaseId).c_str());
            return true;
        }

        static bool HandleSetEnterSpawn(ChatHandler* handler, uint64 phaseId, std::string value)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<bool> enabled;
            if (value == "true" || value == "1")
                enabled = true;
            else if (value == "false" || value == "0")
                enabled = false;
            if (!enabled)
                return Fail(handler, "Значение enter-spawn: true или false.");

            if (!sRoleplayPhaseMgr.SetEnterSpawnEnabled(phaseId, *enabled, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось изменить enter-spawn: для включения сначала задайте spawn.");

            Audit(player, "command_set_enter_spawn", phaseId);
            handler->SendSysMessage(fmt::format("Enter spawn RP phase {} {}.", phaseId, *enabled ? "включён" : "выключен").c_str());
            return true;
        }

        static bool HandleGoto(ChatHandler* handler, uint64 phaseId)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            if (!sRoleplayPhaseMgr.GotoPhase(player, phaseId))
                return Fail(handler, "Переход отклонён: phase недоступна, spawn не задан или teleport не выполнен.");

            Audit(player, "command_goto", phaseId);
            handler->SendSysMessage(fmt::format("Перемещение в RP phase {} выполнено.", phaseId).c_str());
            return true;
        }

        static bool HandleReload(ChatHandler* handler)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            if (!sRoleplayPhaseMgr.Reload())
                return Fail(handler, "Не удалось перезагрузить cache RP phase; предыдущий snapshot сохранён.");

            Audit(player, "command_reload", 0);
            handler->SendSysMessage("Cache RP phase перезагружен.");
            return true;
        }

        static bool HandleSpawnInfo(ChatHandler* handler, std::string type, uint64 spawnId,
            Optional<EXACT_SEQUENCE("--all-phases")> allPhases)
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<RoleplayPhaseSpawnType> spawnType = ParseSpawnType(type);
            if (!spawnType || !spawnId)
                return Fail(handler, "Тип spawn: creature или gameobject; spawn-id должен быть положительным.");

            RoleplayPhaseSpawnInfo info;
            if (!sRoleplayPhaseMgr.GetSpawnInfo(*spawnType, spawnId, info))
                return Fail(handler, "Для этого spawn нет доступного RP phase mapping.");

            bool const bypass = UseAllPhasesBypass(handler, player, !!allPhases, "command_spawn_info_all");
            if (allPhases && !bypass)
                return false;
            if (!bypass && !RequireRole(handler, player, info.PhaseId, RoleplayPhaseRole::Viewer))
                return false;

            handler->SendSysMessage(fmt::format("{} spawn {}: RP phase {}, map {}.",
                SpawnTypeName(info.Type), info.SpawnId, info.PhaseId, info.MapId).c_str());
            return true;
        }

        static bool HandleSpawnAssign(ChatHandler* handler, uint64 phaseId, std::string type, uint64 spawnId,
            EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<RoleplayPhaseSpawnType> spawnType = ParseSpawnType(type);
            if (!spawnType || !spawnId)
                return Fail(handler, "Тип spawn: creature или gameobject; spawn-id должен быть положительным.");

            std::vector<uint64> spawnIds{ spawnId };
            bool const isGroup = *spawnType == RoleplayPhaseSpawnType::GameObject
                && sGobGroupMgr.TryGetGroupSpawnIds(spawnId, spawnIds);
            if (!sRoleplayPhaseMgr.AssignSpawns(phaseId, *spawnType, spawnIds, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось назначить spawn: нужен editor ACL, существующий spawn и подходящий map scope.");

            Audit(player, isGroup ? "command_spawn_assign_group" : "command_spawn_assign", phaseId);
            handler->SendSysMessage(fmt::format("{} spawn {}{} назначен RP phase {} ({} object(s), atomic).",
                SpawnTypeName(*spawnType), spawnId, isGroup ? " group" : "", phaseId, spawnIds.size()).c_str());
            return true;
        }

        static bool HandleSpawnClear(ChatHandler* handler, std::string type, uint64 spawnId, EXACT_SEQUENCE("confirm"))
        {
            Player* player = nullptr;
            if (!RequirePlayer(handler, player))
                return false;

            Optional<RoleplayPhaseSpawnType> spawnType = ParseSpawnType(type);
            if (!spawnType || !spawnId)
                return Fail(handler, "Тип spawn: creature или gameobject; spawn-id должен быть положительным.");

            RoleplayPhaseSpawnInfo info;
            if (!sRoleplayPhaseMgr.GetSpawnInfo(*spawnType, spawnId, info))
                return Fail(handler, "Для этого spawn нет доступного RP phase mapping.");

            std::vector<uint64> spawnIds{ spawnId };
            bool const isGroup = *spawnType == RoleplayPhaseSpawnType::GameObject
                && sGobGroupMgr.TryGetGroupSpawnIds(spawnId, spawnIds);
            if (!sRoleplayPhaseMgr.ClearSpawns(*spawnType, spawnIds, CharacterGuid(player), AccountId(player),
                StaffAccess(player), ServerStaff(player)))
                return Fail(handler, "Не удалось снять mapping: нужен editor ACL в назначенной RP phase.");

            Audit(player, isGroup ? "command_spawn_clear_group" : "command_spawn_clear", info.PhaseId);
            handler->SendSysMessage(fmt::format("{} spawn {}{} снят с RP phase {} ({} object(s), atomic).",
                SpawnTypeName(*spawnType), spawnId, isGroup ? " group" : "", info.PhaseId, spawnIds.size()).c_str());
            return true;
        }

    };
}

void AddSC_NobleNextRoleplayPhaseCommands()
{
    new RoleplayCore::NobleNext::RoleplayPhaseCommands();
}
