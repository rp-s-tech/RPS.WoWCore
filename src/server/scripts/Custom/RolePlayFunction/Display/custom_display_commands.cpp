#include "custom_display_handler.h"
#include "Define.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Common.h"
#include "StringConvert.h"

namespace RoleplayCore
{
    using namespace Trinity::ChatCommands;

    class DisplayCommands : public CommandScript
    {
    public:
        DisplayCommands() : CommandScript("player_display_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable displayCommandTable =
            {
                { "info",      rbac::RBAC_PERM_COMMAND_DISP,            false, Info, "" },
                { "head",      rbac::RBAC_PERM_COMMAND_DISP_HEAD,       false, Display<DISPLAY_TYPE_HEAD>,            "" },
                { "shoulders", rbac::RBAC_PERM_COMMAND_DISP_SHOULDERS,  false, Display<DISPLAY_TYPE_SHOULDERS>,       "" },
                { "lshoulder", rbac::RBAC_PERM_COMMAND_DISP_SHOULDERS,  false, Display<DISPLAY_TYPE_SHOULDERS, true>, "" },
                { "shirt",     rbac::RBAC_PERM_COMMAND_DISP_SHIRT,      false, Display<DISPLAY_TYPE_SHIRT>,           "" },
                { "chest",     rbac::RBAC_PERM_COMMAND_DISP_CHEST,      false, Display<DISPLAY_TYPE_CHEST>,           "" },
                { "waist",     rbac::RBAC_PERM_COMMAND_DISP_WAIST,      false, Display<DISPLAY_TYPE_WAIST>,           "" },
                { "legs",      rbac::RBAC_PERM_COMMAND_DISP_LEGS,       false, Display<DISPLAY_TYPE_PANTS>,           "" },
                { "feet",      rbac::RBAC_PERM_COMMAND_DISP_FEET,       false, Display<DISPLAY_TYPE_BOOTS>,           "" },
                { "wrists",    rbac::RBAC_PERM_COMMAND_DISP_WRISTS,     false, Display<DISPLAY_TYPE_WRISTS>,          "" },
                { "hands",     rbac::RBAC_PERM_COMMAND_DISP_HANDS,      false, Display<DISPLAY_TYPE_HANDS>,           "" },
                { "back",      rbac::RBAC_PERM_COMMAND_DISP_BACK,       false, Display<DISPLAY_TYPE_BACK>,            "" },
                { "tabard",    rbac::RBAC_PERM_COMMAND_DISP_TABARD,     false, Display<DISPLAY_TYPE_TABARD>,          "" },
                { "mainhand",  rbac::RBAC_PERM_COMMAND_DISP_MAINHAND,   false, Display<DISPLAY_TYPE_MAIN>,            "" },
                { "offhand",   rbac::RBAC_PERM_COMMAND_DISP_OFFHAND,    false, Display<DISPLAY_TYPE_OFF>,             "" },
            };

            static ChatCommandTable commandTable =
            {
                { "display", displayCommandTable},
            };

            return commandTable;
        }

        static bool Info(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            static constexpr std::pair<char const*, EquipmentSlots> slots[] = {
                { "head", EQUIPMENT_SLOT_HEAD }, { "shoulders", EQUIPMENT_SLOT_SHOULDERS },
                { "lshoulder", EQUIPMENT_SLOT_SHOULDERS }, { "shirt", EQUIPMENT_SLOT_BODY },
                { "chest", EQUIPMENT_SLOT_CHEST }, { "waist", EQUIPMENT_SLOT_WAIST },
                { "legs", EQUIPMENT_SLOT_LEGS }, { "feet", EQUIPMENT_SLOT_FEET },
                { "wrists", EQUIPMENT_SLOT_WRISTS }, { "hands", EQUIPMENT_SLOT_HANDS },
                { "back", EQUIPMENT_SLOT_BACK }, { "tabard", EQUIPMENT_SLOT_TABARD },
                { "mainhand", EQUIPMENT_SLOT_MAINHAND }, { "offhand", EQUIPMENT_SLOT_OFFHAND }
            };
            // One short line per slot also fits the addon command channel.
            std::string snapshot = "{";
            for (auto const& [key, slot] : slots)
            {
                if (!handler->IsHumanReadable())
                    snapshot += "\n";
                snapshot += "\"";
                snapshot += key;
                snapshot += "\":";
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    bool secondary = std::string_view(key) == "lshoulder";
                    uint32 entry = item->GetVisibleEntry(player);
                    uint32 variant = item->GetVisibleAppearanceModId(player);
                    if (secondary)
                    {
                        auto appearance = sItemModifiedAppearanceStore.LookupEntry(item->GetVisibleSecondaryModifiedAppearanceId(player));
                        entry = appearance ? appearance->ItemID : 0;
                        variant = appearance ? appearance->ItemAppearanceModifierID : 0;
                    }
                    snapshot += "[" + std::to_string(entry) + "," + std::to_string(variant) + "]";
                }
                else
                    snapshot += "null";
                snapshot += ",";
            }
            snapshot.pop_back();
            if (!handler->IsHumanReadable())
                snapshot += "\n";
            snapshot += "}";
            handler->SendSysMessage(snapshot);
            return true;
        }

        // Optimized template method for processing display commands
        template <DisplayType T, bool secondary = false>
        static bool Display(ChatHandler* handler, char const* args)
        {
            if (!handler || !args)
                return false;

            char const* id = handler->extractKeyFromLink((char*)args, "Hitem");
            if (!id)
                return false;

            // "-1" hides the slot visual
            if (!strcmp(id, "-1"))
            {
                DisplayHandler::GetInstance().Hide(handler, T, secondary);
                return true;
            }

            // Retrieve item ID: only an exact "0" resets the slot, a typo must not
            Optional<uint32> itemId = Trinity::StringTo<uint32>(id);
            if (!itemId || (*itemId == 0 && strcmp(id, "0") != 0))
            {
                handler->SendSysMessage("Usage: .disp <slot> <itemId|0|-1> [bonus]");
                handler->SetSentErrorMessage(true);
                return false;
            }

            // Retrieve the bonus, if specified
            uint32 bonus = 0;
            char* bonusStr = strtok(NULL, " ");
            bool exactVariant = bonusStr && bonusStr[0] == 'v';
            if (exactVariant)
            {
                auto variant = Trinity::StringTo<uint32>(bonusStr + 1);
                if (!variant)
                    return false;
                bonus = *variant;
            }
            else if (bonusStr)
                bonus = strtol(bonusStr, NULL, 10);

            // Call the display handler
            DisplayHandler::GetInstance().Display(handler, T, *itemId, bonus, secondary, exactVariant);
            return true;
        }
    };
}

void AddSC_CustomDisplayCommands() { new RoleplayCore::DisplayCommands(); }
