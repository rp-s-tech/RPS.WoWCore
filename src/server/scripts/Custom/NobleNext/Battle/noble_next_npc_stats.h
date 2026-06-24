/*
 * NobleNext — NPC role stats (creature_role_stats).
 */

#pragma once

#include "Define.h"
#include <unordered_map>

class Creature;
class ChatHandler;

namespace RoleplayCore::NobleNext::Battle::NpcStats
{
    enum class Stat : uint8
    {
        Strength = 0,
        Agility  = 1,
        Intellect = 2,
        Stamina  = 3,
        Versa    = 4,
        Will     = 5,
        Health   = 100,
        Armor    = 101,
    };

    void ReloadFromDatabase();
    void SetStat(Creature* creature, Stat stat, int32 value);
    int32 GetStat(Creature* creature, Stat stat);
    void PrintStats(Creature* creature, ChatHandler* handler);
    void ApplyAuraFromStats(Creature* creature);
}
