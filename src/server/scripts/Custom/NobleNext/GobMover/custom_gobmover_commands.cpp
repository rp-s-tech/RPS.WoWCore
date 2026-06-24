/*
 * NobleNext — .movego (GobMover, like GobTele for gameobjects).
 */

#include "noble_next_gobmover_handler.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "RBAC.h"

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

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

        static bool HandleMoveGo(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            if (!GobMoverHandler::Instance().SelectNearestTarget(player))
            {
                handler->SendSysMessage("[GobMover] GO не найден рядом (радиус 20 ярдов).");
                return false;
            }

            return true;
        }
    };
}

void AddSC_NobleNextGobMoverCommands()
{
    new RoleplayCore::NobleNext::GobMoverCommands();
}
