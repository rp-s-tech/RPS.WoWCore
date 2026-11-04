#include "custom_display_handler.h"
#include "TransmogMgr.h"

namespace RoleplayCore
{
    // Initialization of static constants
    const std::map<DisplayType, EquipmentSlots> DisplayHandler::m_item_slots = {
        {DISPLAY_TYPE_HEAD, EQUIPMENT_SLOT_HEAD},
        {DISPLAY_TYPE_SHOULDERS, EQUIPMENT_SLOT_SHOULDERS},
        {DISPLAY_TYPE_SHIRT, EQUIPMENT_SLOT_BODY},
        {DISPLAY_TYPE_CHEST, EQUIPMENT_SLOT_CHEST},
        {DISPLAY_TYPE_WAIST, EQUIPMENT_SLOT_WAIST},
        {DISPLAY_TYPE_PANTS, EQUIPMENT_SLOT_LEGS},
        {DISPLAY_TYPE_BOOTS, EQUIPMENT_SLOT_FEET},
        {DISPLAY_TYPE_WRISTS, EQUIPMENT_SLOT_WRISTS},
        {DISPLAY_TYPE_HANDS, EQUIPMENT_SLOT_HANDS},
        {DISPLAY_TYPE_BACK, EQUIPMENT_SLOT_BACK},
        {DISPLAY_TYPE_TABARD, EQUIPMENT_SLOT_TABARD},
        {DISPLAY_TYPE_MAIN, EQUIPMENT_SLOT_MAINHAND},
        {DISPLAY_TYPE_RANGED, EQUIPMENT_SLOT_MAINHAND},
        {DISPLAY_TYPE_SHIELD, EQUIPMENT_SLOT_OFFHAND},
        {DISPLAY_TYPE_OFF, EQUIPMENT_SLOT_OFFHAND},
    };

    // Weapons have no hidden appearance
    const std::map<DisplayType, uint32> DisplayHandler::m_hidden_items = {
        {DISPLAY_TYPE_HEAD, 134110},      // Hidden Helm
        {DISPLAY_TYPE_SHOULDERS, 134112}, // Hidden Shoulder
        {DISPLAY_TYPE_SHIRT, 142503},     // Hidden Shirt
        {DISPLAY_TYPE_CHEST, 168659},     // Hidden Chestpiece
        {DISPLAY_TYPE_WAIST, 143539},     // Hidden Belt
        {DISPLAY_TYPE_PANTS, 216696},     // Hidden Pants
        {DISPLAY_TYPE_BOOTS, 168664},     // Hidden Boots
        {DISPLAY_TYPE_WRISTS, 168665},    // Hidden Bracers
        {DISPLAY_TYPE_HANDS, 158329},     // Hidden Gloves
        {DISPLAY_TYPE_BACK, 134111},      // Hidden Cloak
        {DISPLAY_TYPE_TABARD, 142504},    // Hidden Tabard
    };

    void DisplayHandler::Display(ChatHandler* handler, DisplayType type, uint32 id, uint32 bonus, bool isSecondary)
    {
        if (!handler || !handler->GetPlayer())
            return;

        auto itemSlot = m_item_slots.at(type);
        auto player = handler->GetPlayer();
        auto item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, itemSlot);

        if (!item)
        {
            handler->SendSysMessage("Slot is empty.");
            return;
        }

        if (id == 0)
        {
            if (isSecondary)
            {
                uint32 savedPrimary = item->GetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS);
                ResetItem(item);
                item->SetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS, savedPrimary);
                item->SetModifier(ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_ALL_SPECS, 0);
            }
            else
            {
                ResetItem(item);
                item->SetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS, 0);
                item->SetModifier(ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_ALL_SPECS, 0);
            }

            player->SetVisibleItemSlot(itemSlot, item);
            item->SetState(ITEM_CHANGED, player);
            return;
        }

        auto itemTemplate = sObjectMgr->GetItemTemplate(id);
        if (!itemTemplate)
        {
            handler->SendSysMessage("Target item not found.");
            return;
        }

        auto applyArtifactAppearance = [&](uint32 artifactAppearanceId) -> bool
        {
            if (!item->GetTemplate()->GetArtifactID() || isSecondary)
                return false;

            ArtifactAppearanceEntry const* artifactAppearance = sArtifactAppearanceStore.LookupEntry(artifactAppearanceId);
            if (!artifactAppearance)
                return false;

            ArtifactAppearanceSetEntry const* artifactAppearanceSet = sArtifactAppearanceSetStore.LookupEntry(artifactAppearance->ArtifactAppearanceSetID);
            if (!artifactAppearanceSet || artifactAppearanceSet->ArtifactID != item->GetTemplate()->GetArtifactID())
                return false;

            item->SetAppearanceModId(artifactAppearance->ItemAppearanceModifierID);
            item->SetModifier(ITEM_MODIFIER_ARTIFACT_APPEARANCE_ID, artifactAppearance->ID);
            item->SetState(ITEM_CHANGED, player);
            player->SetVisibleItemSlot(itemSlot, item);

            Item* childItem = player->GetChildItemByGuid(item->GetChildItem());
            if (childItem)
            {
                childItem->SetAppearanceModId(artifactAppearance->ItemAppearanceModifierID);
                childItem->SetState(ITEM_CHANGED, player);
                player->SetVisibleItemSlot(childItem->GetSlot(), childItem);
            }

            return true;
        };

        if (bonus != 0 && applyArtifactAppearance(bonus))
            return;

        if (bonus != 0)
        {
            if (ItemModifiedAppearanceEntry const* modifiedAppearance = TransmogMgr::GetItemModifiedAppearance(id, bonus))
                if (modifiedAppearance->ItemAppearanceID != 0)
                {
                    ApplyModifiedAppearance(modifiedAppearance->ID, modifiedAppearance->ItemAppearanceID, item, player, handler, itemSlot, isSecondary);
                    return;
                }
        }

        // Default appearance is a map lookup; the full scan below is only a fallback
        if (ItemModifiedAppearanceEntry const* modifiedAppearance = TransmogMgr::GetDefaultItemModifiedAppearance(id))
            if (ApplyModifiedAppearance(modifiedAppearance->ID, modifiedAppearance->ItemAppearanceID, item, player, handler, itemSlot, isSecondary))
                return;

        for (ItemModifiedAppearanceEntry const* modifiedAppearance : sItemModifiedAppearanceStore)
        {
            if (modifiedAppearance->ItemID != id || modifiedAppearance->ItemAppearanceID == 0)
                continue;

            if (ApplyModifiedAppearance(modifiedAppearance->ID, modifiedAppearance->ItemAppearanceID, item, player, handler, itemSlot, isSecondary))
                return;
        }

        handler->SendSysMessage("Target item has no appearance.");
    }

    void DisplayHandler::Hide(ChatHandler* handler, DisplayType type, bool isSecondary)
    {
        if (!handler || !handler->GetPlayer())
            return;

        auto hiddenItem = m_hidden_items.find(type);
        if (hiddenItem == m_hidden_items.end())
        {
            handler->SendSysMessage("This slot can't be hidden.");
            return;
        }

        Display(handler, type, hiddenItem->second, 0, isSecondary);
    }

    void DisplayHandler::ResetItem(Item* item)
    {
        if (!item)
            return;

        // Reset all appearance modifiers
        static const std::array<ItemModifier, 8> modifiers = {
            ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_SPEC_1,
            ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_SPEC_2,
            ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_SPEC_3,
            ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_SPEC_4,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_1,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_2,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_3,
            ITEM_MODIFIER_TRANSMOG_APPEARANCE_SPEC_4
        };

        for (auto mod : modifiers)
            item->SetModifier(mod, 0);
    }

    bool DisplayHandler::ApplyModifiedAppearance(int modApID, int apID, Item* item, Player* player, ChatHandler* handler, EquipmentSlots itemSlot, bool isSecondary)
    {
        if (!item || !player || !handler || apID == 0)
            return false;

        // Any appearance fits any slot: the client draws what it can of it
        if (!sItemAppearanceStore.LookupEntry(apID))
            return false;

        uint32 savedPrimary = item->GetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS);

        ResetItem(item);

        if (isSecondary)
        {
            item->SetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS, savedPrimary);
            item->SetModifier(ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_ALL_SPECS, modApID);
        }
        else
        {
            item->SetModifier(ITEM_MODIFIER_TRANSMOG_APPEARANCE_ALL_SPECS, modApID);
            item->SetModifier(ITEM_MODIFIER_TRANSMOG_SECONDARY_APPEARANCE_ALL_SPECS, modApID);
        }

        // Updating item status and display
        item->SetState(ITEM_CHANGED, player);
        player->SetVisibleItemSlot(itemSlot, item);

        return true;
    }
};

void AddSC_CustomDisplayHandler()
{
    // Just initialize the singleton
    RoleplayCore::DisplayHandler::GetInstance();
}
