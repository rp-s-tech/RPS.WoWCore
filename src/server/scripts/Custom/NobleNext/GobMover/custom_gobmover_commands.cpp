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

        static bool HandleMoveGo(ChatHandler* handler, Optional<GameObjectSpawnId> spawnId)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            if (spawnId)
            {
                ObjectGuid::LowType const id = *spawnId;
                if (!GobMoverHandler::Instance().SelectBySpawnId(player, id))
                {
                    handler->SendSysMessage(fmt::format(
                        "[GobMover] Live GO spawnId {} не загружен на карте игрока "
                        "(.gob near видит DB spawn; подойдите ближе, чтобы грид подгрузил объект).",
                        id).c_str());
                    return true;
                }
                return true;
            }

            if (!GobMoverHandler::Instance().SelectNearestTarget(player))
            {
                handler->SendSysMessage(
                    "[GobMover] GO не найден рядом (радиус 50 ярдов, IgnorePhases). "
                    "Укажите GUID: .movego <guid> (из .gob near).");
                return true;
            }

            return true;
        }
    };
}

void AddSC_NobleNextGobMoverCommands()
{
    new RoleplayCore::NobleNext::GobMoverCommands();
}
