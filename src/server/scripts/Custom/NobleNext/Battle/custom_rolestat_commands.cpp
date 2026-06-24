/*
 * NobleNext — NPC role stats commands + npcroll.
 */

#include "noble_next_battle_constants.h"
#include "noble_next_npc_stats.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "Language.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "SpellAuras.h"

#include <fmt/format.h>
#include <algorithm>
#include <cctype>
#include <optional>
#include <random>
#include <string>

namespace RoleplayCore::NobleNext::Battle
{
    using namespace Trinity::ChatCommands;

    static std::string TrimCopy(std::string value)
    {
        auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    static void StripOuterQuotes(std::string& value)
    {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
    }

    static NpcStats::Stat StatFromCommandId(uint32 cmdId)
    {
        if (cmdId >= 1 && cmdId <= 6)
            return static_cast<NpcStats::Stat>(cmdId - 1);
        if (cmdId == 100 || cmdId == 101)
            return static_cast<NpcStats::Stat>(cmdId);
        return NpcStats::Stat::Strength;
    }

    static std::optional<uint32> RollTypeFromText(std::string text)
    {
        for (char& ch : text)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (text == "1" || text == "str" || text == "strength" || text == "с" || text == "С" || text == "сила")
            return 1;
        if (text == "2" || text == "agi" || text == "agility" || text == "л" || text == "Л" || text == "ловкость")
            return 2;
        if (text == "3" || text == "int" || text == "intellect" || text == "и" || text == "И" || text == "интеллект")
            return 3;

        return std::nullopt;
    }

    static void ApplyHpArmorAuras(Creature* creature, int32 hp, int32 armor)
    {
        if (!creature)
            return;
        creature->RemoveAura(AURA_HP);
        if (hp > 0)
            if (Aura* a = creature->AddAura(AURA_HP, creature))
                a->SetStackAmount(hp);
        creature->RemoveAura(AURA_ARMOR);
        if (armor > 0)
            if (Aura* a = creature->AddAura(AURA_ARMOR, creature))
                a->SetStackAmount(armor);
    }

    class RoleStatCommands : public CommandScript
    {
    public:
        RoleStatCommands() : CommandScript("noble_next_rolestat_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "npcsetstat",       HandleNpcSetStat,       LANG_COMMAND_NPCSTAT_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "npcsetstatradius", HandleNpcSetStatRadius, LANG_COMMAND_NPCSTAT_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "checknpcstat",     HandleCheckNpcStat,     LANG_COMMAND_NPCSTAT_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "reloadnpcstats",   HandleReloadNpcStats,   LANG_COMMAND_NPCSTAT_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "npcroll",          HandleNpcRoll,          LANG_COMMAND_NPCROLL_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleNpcSetStat(ChatHandler* handler, uint32 statId, int32 value)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            Unit* target = handler->getSelectedUnit();
            if (!target || target->IsPlayer())
            {
                handler->SendSysMessage("Выберите NPC.");
                return false;
            }

            NpcStats::Stat stat = StatFromCommandId(statId);
            NpcStats::SetStat(target->ToCreature(), stat, value);
            handler->SendSysMessage(fmt::format("Стата {} = {} для {}", uint32(statId), value, target->GetName()));
            return true;
        }

        static bool HandleNpcSetStatRadius(ChatHandler* handler, int32 str, int32 agi, int32 intel,
            int32 vit, int32 dex, int32 will, int32 hp, int32 armor)
        {
            Player* player = handler->GetPlayer();
            if (!player || player->GetSession()->GetSecurity() <= SEC_PLAYER)
                return false;

            Creature* target = handler->getSelectedCreature();
            if (!target)
            {
                handler->SendSysMessage("Выберите NPC.");
                return false;
            }

            auto apply = [&](Creature* c)
            {
                NpcStats::SetStat(c, NpcStats::Stat::Strength, str);
                NpcStats::SetStat(c, NpcStats::Stat::Agility, agi);
                NpcStats::SetStat(c, NpcStats::Stat::Intellect, intel);
                NpcStats::SetStat(c, NpcStats::Stat::Stamina, vit);
                NpcStats::SetStat(c, NpcStats::Stat::Versa, dex);
                NpcStats::SetStat(c, NpcStats::Stat::Will, will);
                NpcStats::SetStat(c, NpcStats::Stat::Health, hp);
                NpcStats::SetStat(c, NpcStats::Stat::Armor, armor);
                ApplyHpArmorAuras(c, hp, armor);
            };

            apply(target);

            std::list<Creature*> creatures;
            Trinity::AllCreaturesOfEntryInRange check(target, target->GetEntry(), 50.f);
            Trinity::CreatureListSearcher<Trinity::AllCreaturesOfEntryInRange> searcher(target, creatures, check);
            Cell::VisitGridObjects(target, searcher, 50.f);

            for (Creature* c : creatures)
                if (c != target)
                    apply(c);

            handler->SendSysMessage("Статы установлены (цель + радиус 50).");
            return true;
        }

        static bool HandleCheckNpcStat(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            if (!player || player->GetSession()->GetSecurity() <= SEC_PLAYER)
                return false;

            Creature* target = handler->getSelectedCreature();
            if (!target)
            {
                handler->SendSysMessage("Выберите NPC.");
                return false;
            }

            NpcStats::PrintStats(target, handler);
            return true;
        }

        static bool HandleReloadNpcStats(ChatHandler* handler)
        {
            if (!handler->GetPlayer() || handler->GetPlayer()->GetSession()->GetSecurity() <= SEC_PLAYER)
                return false;
            NpcStats::ReloadFromDatabase();
            handler->SendSysMessage("Статы NPC перезагружены из БД.");
            return true;
        }

        static bool HandleNpcRoll(ChatHandler* handler, Tail tail)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            Creature* roller = handler->getSelectedCreature();
            if (!roller)
            {
                handler->SendSysMessage("Выберите NPC (атакующий).");
                return false;
            }

            if (!CanControlOwnedCreature(player, roller) && player->GetSession()->GetSecurity() <= SEC_PLAYER)
            {
                handler->SendSysMessage("NPC вам не принадлежит.");
                return false;
            }

            std::string args = TrimCopy(std::string(tail));
            uint32 rollType = 1;
            std::string targetName;

            if (!args.empty())
            {
                size_t lastSpace = args.rfind(' ');
                if (lastSpace != std::string::npos)
                {
                    std::string maybeType = TrimCopy(args.substr(lastSpace + 1));
                    if (std::optional<uint32> parsedType = RollTypeFromText(maybeType))
                    {
                        rollType = *parsedType;
                        targetName = TrimCopy(args.substr(0, lastSpace));
                        StripOuterQuotes(targetName);
                    }
                    else
                    {
                        targetName = args;
                        StripOuterQuotes(targetName);
                    }
                }
                else if (std::optional<uint32> parsedType = RollTypeFromText(args))
                {
                    rollType = *parsedType;
                }
                else
                {
                    targetName = args;
                    StripOuterQuotes(targetName);
                }
            }

            Player* target = nullptr;
            if (!targetName.empty())
            {
                target = ObjectAccessor::FindPlayerByName(targetName);
                if (!target)
                    target = handler->getSelectedPlayer();
                if (!target)
                {
                    handler->SendSysMessage("Игрок не найден.");
                    return false;
                }
            }

            uint32 statIndex = 0;
            char const* statLabel = "Сила";
            if (rollType == 2) { statIndex = 1; statLabel = "Ловкость"; }
            else if (rollType == 3) { statIndex = 2; statLabel = "Интеллект"; }

            int32 statVal = NpcStats::GetStat(roller, static_cast<NpcStats::Stat>(statIndex));
            std::mt19937 rng{ std::random_device{}() };
            int32 roll = int32(std::uniform_int_distribution<int>(1, 20)(rng));
            int32 total = roll + statVal;

            if (target)
                handler->SendSysMessage(fmt::format("{} бросает {} против {}: d20({}) + {}({}) = {}",
                    roller->GetName(), statLabel, target->GetName(), roll, statLabel, statVal, total));
            else
                handler->SendSysMessage(fmt::format("{} бросает {}: d20({}) + {}({}) = {}",
                    roller->GetName(), statLabel, roll, statLabel, statVal, total));
            return true;
        }
    };
}

void AddSC_NobleNextRoleStatCommands()
{
    new RoleplayCore::NobleNext::Battle::RoleStatCommands();
}
