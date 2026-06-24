/*
 * NobleNext — army controller commands.
 */

#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellAuras.h"

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    constexpr uint32 AURA_NPC_CONTROL = 540626;

    class ArmyCommands : public CommandScript
    {
    public:
        ArmyCommands() : CommandScript("noble_next_army_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "npccontrol",    HandleNpcControl,    rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "deleteselected", HandleDeleteSelected, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleNpcControl(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;
            player->AddAura(AURA_NPC_CONTROL, player);
            handler->SendSysMessage("Режим Army Controller активирован.");
            return true;
        }

        static bool HandleDeleteSelected(ChatHandler* handler)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;
            handler->SendSysMessage("Используйте /nnarmy или AIO ArmyHandlers для удаления выделенных NPC.");
            return true;
        }
    };
}

void AddSC_NobleNextArmyCommands()
{
    new RoleplayCore::NobleNext::ArmyCommands();
}
