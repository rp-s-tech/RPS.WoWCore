/*
 * NobleNext — .gobject tele / .gobject teleport (set GO gossip teleport destination).
 *
 * Nested under vanilla `gobject` so `.gob …` is no longer ambiguous with a
 * top-level `.gobtele` (both names start with "gob").
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
            // Merges into cs_gobject.cpp's `gobject` node (LoadFromBuilder appends subcommands).
            static ChatCommandTable gobjectTeleTable =
            {
                { "tele",     HandleGobTele, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "teleport", HandleGobTele, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };

            static ChatCommandTable commandTable =
            {
                { "gobject", gobjectTeleTable },
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
