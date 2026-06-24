/*
 * NobleNext — master panel chat fallback commands.
 */

#include "noble_next_npc_pose.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Creature.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "SharedDefines.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    static Unit* GetChatTarget(ChatHandler* handler)
    {
        Unit* unit = handler->getSelectedUnit();
        if (!unit)
            handler->SendSysMessage("Выберите цель.");
        return unit;
    }

    class MasterCommands : public CommandScript
    {
    public:
        MasterCommands() : CommandScript("noble_next_master_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "nnsay",       HandleNnSay,       rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "nnsayemote",  HandleNnSayEmote,  rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "nnemote",     HandleNnEmote,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "nnyell",      HandleNnYell,      rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "weapon",      HandleWeapon,      LANG_COMMAND_WEAPON_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "npcsave",     HandleNpcSave,     LANG_COMMAND_NPCSAVE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleNnSay(ChatHandler* handler, Tail text)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;
            Unit* unit = GetChatTarget(handler);
            if (!unit) return false;
            unit->Say(std::string(text), LANG_UNIVERSAL);
            return true;
        }

        static bool HandleNnSayEmote(ChatHandler* handler, Tail text)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;
            Unit* unit = GetChatTarget(handler);
            if (!unit) return false;
            std::string line(text);
            if (!line.empty())
            {
                char last = line.back();
                if (last == '!') unit->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                else if (last == '?') unit->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
                else unit->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
            }
            unit->Say(line, LANG_UNIVERSAL);
            return true;
        }

        static bool HandleNnEmote(ChatHandler* handler, Tail text)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;
            Unit* unit = GetChatTarget(handler);
            if (!unit) return false;
            unit->TextEmote(std::string(text), handler->GetPlayer());
            return true;
        }

        static bool HandleNnYell(ChatHandler* handler, Tail text)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;
            Unit* unit = GetChatTarget(handler);
            if (!unit) return false;
            unit->Yell(std::string(text), LANG_UNIVERSAL);
            return true;
        }

        static bool HandleWeapon(ChatHandler* handler, Optional<uint8> slot)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;

            Creature* creature = handler->getSelectedCreature();
            if (!creature)
            {
                handler->SendSysMessage("Выберите NPC.");
                return false;
            }

            uint8 next = slot ? *slot : (static_cast<uint8>(creature->GetSheath()) + 1) % MAX_SHEATH_STATE;
            if (next >= MAX_SHEATH_STATE)
                next = SHEATH_STATE_UNARMED;

            creature->SetSheath(static_cast<SheathState>(next));
            handler->SendSysMessage(fmt::format("Оружие NPC: режим {}.", uint32(next)));
            return true;
        }

        static bool HandleNpcSave(ChatHandler* handler)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;

            Creature* creature = handler->getSelectedCreature();
            if (!creature)
            {
                handler->SendSysMessage("Выберите NPC.");
                return false;
            }

            return NpcPoseService::Instance().SaveCreaturePose(creature, handler->GetPlayer(), handler);
        }
    };
}

void AddSC_NobleNextMasterCommands()
{
    new RoleplayCore::NobleNext::MasterCommands();
}
