/*
 * NobleNext — .gobtele command.
 */

#include "noble_next_gobtele_handler.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "RBAC.h"

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class GobTeleCommands : public CommandScript
    {
    public:
        GobTeleCommands() : CommandScript("noble_next_gobtele_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "gobtele", HandleGobTele, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleGobTele(ChatHandler* handler, uint32 goGuidLow)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            if (!GobTeleHandler::Instance().SetTeleportDestination(player, goGuidLow))
            {
                handler->SendSysMessage("Ошибка — неверно введён GUID объекта.");
                return false;
            }
            return true;
        }
    };
}

void AddSC_NobleNextGobTeleCommands()
{
    new RoleplayCore::NobleNext::GobTeleCommands();
}
