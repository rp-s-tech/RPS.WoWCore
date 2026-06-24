/*
 * NobleNext — NPC role stats.
 */

#include "noble_next_npc_stats.h"
#include "noble_next_battle_constants.h"

#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "SpellAuras.h"
#include "WorldDatabase.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext::Battle::NpcStats
{
    namespace
    {
        std::unordered_map<uint32, std::unordered_map<uint8, int32>> s_stats;

        char const* ColumnForStat(Stat stat)
        {
            switch (stat)
            {
                case Stat::Strength:  return "STR";
                case Stat::Agility:   return "AGI";
                case Stat::Intellect: return "INTEL";
                case Stat::Stamina:   return "VIT";
                case Stat::Versa:     return "DEX";
                case Stat::Will:      return "WILL";
                case Stat::Health:    return "HEALTH";
                case Stat::Armor:     return "ARMOR";
                default: return nullptr;
            }
        }
    }

    void ReloadFromDatabase()
    {
        s_stats.clear();
        QueryResult result = WorldDatabase.Query(
            "SELECT guid, STR, AGI, INTEL, VIT, DEX, WILL, HEALTH, ARMOR FROM creature_role_stats");
        if (!result)
            return;

        do
        {
            Field* f = result->Fetch();
            uint32 guid = f[0].GetUInt32();
            auto& row = s_stats[guid];
            row[uint8(Stat::Strength)]  = f[1].GetInt32();
            row[uint8(Stat::Agility)]   = f[2].GetInt32();
            row[uint8(Stat::Intellect)] = f[3].GetInt32();
            row[uint8(Stat::Stamina)]   = f[4].GetInt32();
            row[uint8(Stat::Versa)]     = f[5].GetInt32();
            row[uint8(Stat::Will)]      = f[6].GetInt32();
            row[uint8(Stat::Health)]    = f[7].GetInt32();
            row[uint8(Stat::Armor)]     = f[8].GetInt32();
        } while (result->NextRow());
    }

    void SetStat(Creature* creature, Stat stat, int32 value)
    {
        if (!creature)
            return;

        uint32 guid = creature->GetSpawnId();
        s_stats[guid][uint8(stat)] = value;

        char const* col = ColumnForStat(stat);
        if (!col)
            return;

        QueryResult exists = WorldDatabase.PQuery(
            "SELECT 1 FROM creature_role_stats WHERE guid = {} LIMIT 1", guid);
        if (!exists)
        {
            WorldDatabase.PExecute(
                "INSERT INTO creature_role_stats (guid, STR, AGI, INTEL, VIT, DEX, WILL, SPI, HEALTH, ARMOR) "
                "VALUES ({}, 0, 0, 0, 0, 0, 0, 0, 0, 0)", guid);
        }

        WorldDatabase.PExecute(
            "UPDATE creature_role_stats SET {} = {} WHERE guid = {}", col, value, guid);

        if (stat == Stat::Health || stat == Stat::Armor)
            ApplyAuraFromStats(creature);
    }

    int32 GetStat(Creature* creature, Stat stat)
    {
        if (!creature)
            return 0;
        uint32 guid = creature->GetSpawnId();
        auto git = s_stats.find(guid);
        if (git == s_stats.end())
            return 0;
        auto sit = git->second.find(uint8(stat));
        return sit != git->second.end() ? sit->second : 0;
    }

    void ApplyAuraFromStats(Creature* creature)
    {
        if (!creature)
            return;

        int32 hp = GetStat(creature, Stat::Health);
        creature->RemoveAura(AURA_HP);
        if (hp > 0)
            if (Aura* a = creature->AddAura(AURA_HP, creature))
                a->SetStackAmount(hp);

        int32 armor = GetStat(creature, Stat::Armor);
        creature->RemoveAura(AURA_ARMOR);
        if (armor > 0)
            if (Aura* a = creature->AddAura(AURA_ARMOR, creature))
                a->SetStackAmount(armor);
    }

    void PrintStats(Creature* creature, ChatHandler* handler)
    {
        if (!creature || !handler)
            return;

        handler->SendSysMessage(fmt::format("Сила: {}", GetStat(creature, Stat::Strength)));
        handler->SendSysMessage(fmt::format("Ловк: {}", GetStat(creature, Stat::Agility)));
        handler->SendSysMessage(fmt::format("Инта: {}", GetStat(creature, Stat::Intellect)));
        handler->SendSysMessage(fmt::format("Физ.уст: {}", GetStat(creature, Stat::Versa)));
        handler->SendSysMessage(fmt::format("Маг.уст: {}", GetStat(creature, Stat::Will)));
    }
}
