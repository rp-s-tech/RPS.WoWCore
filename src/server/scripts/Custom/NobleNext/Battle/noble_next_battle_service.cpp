/*
 * NobleNext Battle — aura stack helpers.
 */

#include "noble_next_battle_service.h"
#include "noble_next_npc_stats.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "Player.h"
#include "SpellAuras.h"
#include "Unit.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext::Battle
{
    static void NotifyTarget(Player* targetPlayer, std::string const& message)
    {
        if (targetPlayer)
            ChatHandler(targetPlayer->GetSession()).SendSysMessage(message);
    }

    bool SetAuraStack(Unit* target, uint32 spellId, int32 value, ChatHandler* handler, char const* label)
    {
        if (!target || !handler)
            return false;

        if (value <= 0)
        {
            target->RemoveAura(spellId);
            return true;
        }

        if (Aura* aura = target->GetAura(spellId))
            aura->SetStackAmount(value);
        else if (Aura* added = target->AddAura(spellId, target))
            added->SetStackAmount(value);
        else
        {
            handler->SendSysMessage("Не удалось наложить ауру.");
            return false;
        }

        handler->SendSysMessage(fmt::format("{} установлено {}: {}", target->GetName(), label, value));
        return true;
    }

    bool AddAuraStack(Unit* target, uint32 spellId, int32 delta, ChatHandler* handler, char const* label)
    {
        if (!target || !handler)
            return false;

        if (delta == 0)
            return true;

        Aura* aura = target->GetAura(spellId);
        if (!aura)
        {
            if (delta < 0)
            {
                handler->SendSysMessage("Существо не имеет ауры.");
                return false;
            }
            return SetAuraStack(target, spellId, delta, handler, label);
        }

        int32 stacks = aura->GetStackAmount() + delta;
        if (stacks < 1)
        {
            target->RemoveAura(spellId);
            return true;
        }

        aura->SetStackAmount(stacks);
        handler->SendSysMessage(fmt::format("{} изменено {}: {}", target->GetName(), label, stacks));
        return true;
    }

    bool RemoveAuraStack(Unit* target, uint32 spellId, int32 delta, ChatHandler* handler, char const* label)
    {
        return AddAuraStack(target, spellId, -delta, handler, label);
    }

    bool SetIndexedAura(Unit* target, AuraEntry const* entry, int32 value, ChatHandler* handler)
    {
        if (!entry || !handler)
        {
            if (handler)
                handler->SendSysMessage("Неверный индекс эффекта.");
            return false;
        }
        return SetAuraStack(target, entry->SpellId, value, handler, entry->Name.data());
    }

    bool RemoveIndexedAura(Unit* target, AuraEntry const* entry, ChatHandler* handler)
    {
        if (!entry || !handler)
        {
            if (handler)
                handler->SendSysMessage("Неверный индекс эффекта.");
            return false;
        }
        target->RemoveAura(entry->SpellId);
        handler->SendSysMessage(fmt::format("Снят эффект: {}", entry->Name));
        return true;
    }

    void WakeupTarget(Unit* target, Player* issuer, ChatHandler* handler)
    {
        if (!target)
            return;

        target->RemoveAura(AURA_DEATH);
        target->RemoveAura(AURA_HP);
        target->RemoveAura(AURA_ARMOR);
        target->RemoveAura(AURA_ENERGY);
        target->RemoveAura(AURA_FOCUS);

        if (!target->IsPlayer())
        {
            NpcStats::SetStat(target->ToCreature(), NpcStats::Stat::Health, 0);
            NpcStats::SetStat(target->ToCreature(), NpcStats::Stat::Armor, 0);
        }

        for (AuraEntry const& e : Auras)
            target->RemoveAura(e.SpellId);

        if (handler)
            handler->SendSysMessage(fmt::format("Пробуждение: {}", target->GetName()));
    }
}
