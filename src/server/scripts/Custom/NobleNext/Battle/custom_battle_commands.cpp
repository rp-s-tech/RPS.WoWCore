/*
 * NobleNext — battle/status GM commands (EBS).
 */

#include "noble_next_battle_constants.h"
#include "noble_next_battle_service.h"
#include "noble_next_npc_stats.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Creature.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"
#include "Unit.h"

namespace RoleplayCore::NobleNext::Battle
{
    using namespace Trinity::ChatCommands;

    static Unit* GetPermittedTarget(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        Unit* target = handler->getSelectedUnit();
        if (!player || !target)
        {
            handler->SendSysMessage("Выберите цель.");
            return nullptr;
        }
        if (!CanControlBattleTarget(player, target))
        {
            handler->SendSysMessage("Недостаточно прав для этой цели.");
            return nullptr;
        }
        return target;
    }

    static bool HandleAuraSet(ChatHandler* handler, uint32 spellId, int32 value, char const* label,
        std::optional<NpcStats::Stat> npcStat = std::nullopt)
    {
        Unit* target = GetPermittedTarget(handler);
        if (!target)
            return false;

        if (npcStat && !target->IsPlayer())
            NpcStats::SetStat(target->ToCreature(), *npcStat, value);

        return SetAuraStack(target, spellId, value, handler, label);
    }

    static bool HandleAuraAdd(ChatHandler* handler, uint32 spellId, int32 delta, char const* label,
        std::optional<NpcStats::Stat> npcStat = std::nullopt)
    {
        Unit* target = GetPermittedTarget(handler);
        if (!target)
            return false;

        if (npcStat && !target->IsPlayer())
        {
            int32 cur = NpcStats::GetStat(target->ToCreature(), *npcStat);
            NpcStats::SetStat(target->ToCreature(), *npcStat, cur + delta);
        }

        return AddAuraStack(target, spellId, delta, handler, label);
    }

    class BattleCommands : public CommandScript
    {
    public:
        BattleCommands() : CommandScript("noble_next_battle_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            // Canonical: .rps character set|add|dam|remove <stat>
            // Legacy flat aliases (.sethp, …) kept for Battle UI / muscle memory.
            static ChatCommandTable charSetTable =
            {
                { "hp",       HandleSetHp,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "armor",    HandleSetArmor,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "pharmor",  HandleSetPhArmor,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "magarmor", HandleSetMagArmor, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "en",       HandleSetEn,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "energy",   HandleSetEn,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "focus",    HandleSetFocus,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wound",    HandleSetWound,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "status",   HandleSetStatus,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "aurastats", HandleSetAuraStats, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "buff",     HandleSetBuff,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "debuff",   HandleSetDebuff,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "harm",     HandleSetHarm,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "actions",  HandleSetActions,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            static ChatCommandTable charAddTable =
            {
                { "hp",       HandleAddHp,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "armor",    HandleAddArmor,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "pharmor",  HandleAddPhArmor,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "magarmor", HandleAddMagArmor, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "en",       HandleAddEn,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "energy",   HandleAddEn,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "focus",    HandleAddFocus,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            static ChatCommandTable charDamTable =
            {
                { "hp",     HandleDamHp,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "en",     HandleDamEn,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "energy", HandleDamEn,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            static ChatCommandTable charRemoveTable =
            {
                { "armor",    HandleRemoveArmor,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "pharmor",  HandleRemovePhArmor,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "magarmor", HandleRemoveMagArmor, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "focus",    HandleRemoveFocus,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "status",   HandleRemoveStatus,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "aurastats", HandleRemoveAuraStats, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "buff",     HandleRemoveBuff,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "debuff",   HandleRemoveDebuff,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "harm",     HandleRemoveHarm,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "actions",  HandleRemoveActions,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            static ChatCommandTable characterTable =
            {
                { "set",    charSetTable },
                { "add",    charAddTable },
                { "dam",    charDamTable },
                { "remove", charRemoveTable },
                { "wakeup", HandleWakeup, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            static ChatCommandTable rpsTable =
            {
                { "character", characterTable },
            };

            static ChatCommandTable commandTable =
            {
                { "rps", rpsTable },
                { "sethp",         HandleSetHp,         LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "addhp",         HandleAddHp,         LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "damhp",         HandleDamHp,         LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setarmor",      HandleSetArmor,      LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "addarmor",      HandleAddArmor,      LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removearmor",   HandleRemoveArmor,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setpharmor",    HandleSetPhArmor,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "addpharmor",    HandleAddPhArmor,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removepharmor", HandleRemovePhArmor, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setmagarmor",   HandleSetMagArmor,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "addmagarmor",   HandleAddMagArmor,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removemagarmor", HandleRemoveMagArmor, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setwound",      HandleSetWound,      LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "seten",         HandleSetEn,         LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "adden",         HandleAddEn,         LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "damen",         HandleDamEn,         LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setfocus",      HandleSetFocus,      LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "addfocus",      HandleAddFocus,      LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removefocus",   HandleRemoveFocus,   LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setstatus",     HandleSetStatus,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removestatus",  HandleRemoveStatus,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setaurastats",  HandleSetAuraStats,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removeaurastats", HandleRemoveAuraStats, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setbuff",       HandleSetBuff,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removebuff",    HandleRemoveBuff,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setdebuff",     HandleSetDebuff,     LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removedebuff",  HandleRemoveDebuff,  LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setharm",       HandleSetHarm,       LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removeharm",    HandleRemoveHarm,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setactions",    HandleSetActions,    LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "removeactions", HandleRemoveActions, LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wakeup",        HandleWakeup,        LANG_COMMAND_BATTLE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleSetHp(ChatHandler* h, int32 v) { return HandleAuraSet(h, AURA_HP, v, "HP", NpcStats::Stat::Health); }
        static bool HandleAddHp(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_HP, v, "HP", NpcStats::Stat::Health); }
        static bool HandleDamHp(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_HP, -v, "HP", NpcStats::Stat::Health); }
        static bool HandleSetArmor(ChatHandler* h, int32 v) { return HandleAuraSet(h, AURA_ARMOR, v, "броня", NpcStats::Stat::Armor); }
        static bool HandleAddArmor(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_ARMOR, v, "броня", NpcStats::Stat::Armor); }
        static bool HandleRemoveArmor(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_ARMOR, -v, "броня", NpcStats::Stat::Armor); }
        static bool HandleSetPhArmor(ChatHandler* h, int32 v) { return HandleAuraSet(h, AURA_PHYS_DEF, v, "физ.защита"); }
        static bool HandleAddPhArmor(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_PHYS_DEF, v, "физ.защита"); }
        static bool HandleRemovePhArmor(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_PHYS_DEF, -v, "физ.защита"); }
        static bool HandleSetMagArmor(ChatHandler* h, int32 v) { return HandleAuraSet(h, AURA_MAG_DEF, v, "маг.защита"); }
        static bool HandleAddMagArmor(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_MAG_DEF, v, "маг.защита"); }
        static bool HandleRemoveMagArmor(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_MAG_DEF, -v, "маг.защита"); }
        static bool HandleSetEn(ChatHandler* h, int32 v) { return HandleAuraSet(h, AURA_ENERGY, v, "энергия"); }
        static bool HandleAddEn(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_ENERGY, v, "энергия"); }
        static bool HandleDamEn(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_ENERGY, -v, "энергия"); }
        static bool HandleSetFocus(ChatHandler* h, int32 v) { return HandleAuraSet(h, AURA_FOCUS, v, "фокус"); }
        static bool HandleAddFocus(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_FOCUS, v, "фокус"); }
        static bool HandleRemoveFocus(ChatHandler* h, int32 v) { return HandleAuraAdd(h, AURA_FOCUS, -v, "фокус"); }

        static bool HandleSetWound(ChatHandler* h, int32 v)
        {
            Player* player = h->GetPlayer();
            if (!player || player->GetSession()->GetSecurity() <= SEC_PLAYER)
                return false;
            Unit* target = h->getSelectedUnit();
            if (!target) { h->SendSysMessage("Выберите цель."); return false; }
            return SetAuraStack(target, AURA_WOUND, v, h, "раны");
        }

        static bool HandleSetStatus(ChatHandler* h, uint32 id, int32 v)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(Auras, std::size(Auras), id);
            if (!entry) { h->SendSysMessage("Неверный ID статуса."); return false; }
            return SetIndexedAura(target, *entry, v, h);
        }

        static bool HandleRemoveStatus(ChatHandler* h, uint32 id)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(Auras, std::size(Auras), id);
            if (!entry) return false;
            return RemoveIndexedAura(target, *entry, h);
        }

        static bool HandleSetAuraStats(ChatHandler* h, uint32 id, int32 v)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraStats, std::size(AuraStats), id);
            if (!entry) return false;
            return SetIndexedAura(target, *entry, v, h);
        }

        static bool HandleRemoveAuraStats(ChatHandler* h, uint32 id)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraStats, std::size(AuraStats), id);
            if (!entry) return false;
            return RemoveIndexedAura(target, *entry, h);
        }

        static bool HandleSetBuff(ChatHandler* h, uint32 id, int32 v)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraBuffs, std::size(AuraBuffs), id);
            if (!entry) return false;
            return SetIndexedAura(target, *entry, v, h);
        }

        static bool HandleRemoveBuff(ChatHandler* h, uint32 id)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraBuffs, std::size(AuraBuffs), id);
            if (!entry) return false;
            return RemoveIndexedAura(target, *entry, h);
        }

        static bool HandleSetDebuff(ChatHandler* h, uint32 id, int32 v)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraDebuffs, std::size(AuraDebuffs), id);
            if (!entry) return false;
            return SetIndexedAura(target, *entry, v, h);
        }

        static bool HandleRemoveDebuff(ChatHandler* h, uint32 id)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraDebuffs, std::size(AuraDebuffs), id);
            if (!entry) return false;
            return RemoveIndexedAura(target, *entry, h);
        }

        static bool HandleSetHarm(ChatHandler* h, uint32 id, int32 v)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraHarm, std::size(AuraHarm), id);
            if (!entry) return false;
            return SetIndexedAura(target, *entry, v, h);
        }

        static bool HandleRemoveHarm(ChatHandler* h, uint32 id)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraHarm, std::size(AuraHarm), id);
            if (!entry) return false;
            return RemoveIndexedAura(target, *entry, h);
        }

        static bool HandleSetActions(ChatHandler* h, uint32 id, int32 v)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraActions, std::size(AuraActions), id);
            if (!entry) return false;
            return SetIndexedAura(target, *entry, v, h);
        }

        static bool HandleRemoveActions(ChatHandler* h, uint32 id)
        {
            Unit* target = GetPermittedTarget(h);
            if (!target) return false;
            auto entry = ResolveIndexed(AuraActions, std::size(AuraActions), id);
            if (!entry) return false;
            return RemoveIndexedAura(target, *entry, h);
        }

        static bool HandleWakeup(ChatHandler* h)
        {
            Player* player = h->GetPlayer();
            if (!player)
                return false;

            Unit* target = h->getSelectedUnit();
            if (player->GetSession()->GetSecurity() > SEC_PLAYER && target)
                WakeupTarget(target, player, h);
            else
                WakeupTarget(player, player, h);
            return true;
        }
    };
}

void AddSC_NobleNextBattleCommands()
{
    new RoleplayCore::NobleNext::Battle::BattleCommands();
}
