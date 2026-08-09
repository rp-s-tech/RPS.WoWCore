/*
 * NobleNext — .movego (GobMover, like GobTele for gameobjects).
 */

#include "noble_next_gobmover_handler.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Player.h"
#include "RBAC.h"
#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayPhaseMgr.h"
#include "ScriptMgr.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;
    using GameObjectSpawnId = Variant<Hyperlink<gameobject>, ObjectGuid::LowType>;

    class GobMoverCommands : public CommandScript
    {
    public:
        GobMoverCommands() : CommandScript("noble_next_gobmover_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "movego", HandleMoveGo, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleMoveGo(ChatHandler* handler, Optional<GameObjectSpawnId> spawnId,
            Optional<EXACT_SEQUENCE("--all-phases")> crossPhaseFlag)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            bool const requestedCrossPhase = !!crossPhaseFlag;
            if (requestedCrossPhase && !spawnId)
            {
                handler->SendSysMessage("[GobMover] --all-phases допустим только с явным GUID цели.");
                return false;
            }
            if (requestedCrossPhase
                && !player->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_RP_PHASE_ALL_PHASES))
            {
                handler->SendSysMessage("[GobMover] Для cross-phase выбора требуется отдельное право RP phase --all-phases.");
                return false;
            }

            if (spawnId)
            {
                ObjectGuid::LowType const id = *spawnId;
                uint64 const activePhaseId = sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId());
                uint64 const targetPhaseId = sRoleplayPhaseMgr.GetSpawnPhaseId(RoleplayPhaseSpawnType::GameObject, id, player->GetMapId());
                bool const isCrossPhase = activePhaseId != targetPhaseId;
                if (isCrossPhase && !requestedCrossPhase)
                {
                    handler->SendSysMessage(fmt::format(
                        "[GobMover] GO {} находится в logical RP phase {} (ваша: {}). "
                        "Для staff cross-phase выбора подтвердите: .movego {} --all-phases",
                        id, targetPhaseId, activePhaseId, id).c_str());
                    return false;
                }

                uint64 selectedPhaseId = 0;
                if (!GobMoverHandler::Instance().SelectBySpawnId(player, id, requestedCrossPhase, &selectedPhaseId))
                {
                    handler->SendSysMessage(fmt::format(
                        "[GobMover] Live GO spawnId {} не загружен на карте игрока "
                        "(.gob near видит DB spawn; подойдите ближе, чтобы грид подгрузил объект).",
                        id).c_str());
                    return true;
                }
                if (!RoleplayCommandPhaseGuard::AuthorizeGobMoverTarget(player,
                    RoleplayPhaseSpawnType::GameObject, id, requestedCrossPhase))
                {
                    handler->SendSysMessage("[GobMover] Не удалось авторизовать выбранную цель.");
                    return false;
                }
                handler->SendSysMessage(fmt::format("[GobMover] Выбрана цель {} (logical RP phase {}).",
                    id, selectedPhaseId).c_str());
                if (isCrossPhase)
                    sRoleplayPhaseMgr.WriteAudit(player->GetGUID().GetCounter(), player->GetSession()->GetAccountId(),
                        "gobmover_cross_phase_select", selectedPhaseId,
                        fmt::format(R"({{"spawn_id":{},"source_phase":{}}})", id, activePhaseId));
                return true;
            }

            uint64 targetPhaseId = 0;
            ObjectGuid::LowType selectedSpawnId = 0;
            if (!GobMoverHandler::Instance().SelectNearestTarget(player, &targetPhaseId, &selectedSpawnId))
            {
                handler->SendSysMessage(
                    "[GobMover] GO не найден рядом (радиус 50 ярдов, текущая logical RP phase). "
                    "Проверьте .gob near или укажите GUID: .movego <guid>.");
                return true;
            }

            if (!RoleplayCommandPhaseGuard::AuthorizeGobMoverTarget(player,
                RoleplayPhaseSpawnType::GameObject, selectedSpawnId, false))
            {
                handler->SendSysMessage("[GobMover] Не удалось авторизовать выбранную цель.");
                return false;
            }
            handler->SendSysMessage(fmt::format("[GobMover] Выбрана цель в logical RP phase {}.", targetPhaseId).c_str());
            return true;
        }
    };

    class GobMoverSessionScript : public PlayerScript
    {
    public:
        GobMoverSessionScript() : PlayerScript("noble_next_gobmover_session") { }

        void OnLogout(Player* player) override
        {
            RoleplayCommandPhaseGuard::RevokeGobMoverAuthorization(player);
        }
    };
}

void AddSC_NobleNextGobMoverCommands()
{
    new RoleplayCore::NobleNext::GobMoverCommands();
    new RoleplayCore::NobleNext::GobMoverSessionScript();
}
