/*
 * NobleNext Battle — startup hooks.
 */

#include "noble_next_npc_stats.h"

#include "ScriptMgr.h"

namespace RoleplayCore::NobleNext::Battle
{
    class noble_next_battle_world_script : public WorldScript
    {
    public:
        noble_next_battle_world_script() : WorldScript("noble_next_battle_world_script") { }

        void OnStartup() override
        {
            NpcStats::ReloadFromDatabase();
        }
    };
}

void AddSC_NobleNextBattleScripts()
{
    new RoleplayCore::NobleNext::Battle::noble_next_battle_world_script();
}
