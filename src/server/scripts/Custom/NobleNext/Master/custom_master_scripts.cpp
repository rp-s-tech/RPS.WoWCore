/*
 * NobleNext Master — startup / login hooks for saved NPC poses.
 */

#include "noble_next_npc_pose.h"

#include "Player.h"
#include "ScriptMgr.h"

namespace RoleplayCore::NobleNext
{
    class master_world_script : public WorldScript
    {
    public:
        master_world_script() : WorldScript("noble_next_master_world_script") { }

        void OnStartup() override
        {
            NpcPoseService::Instance().LoadFromDatabase();
        }
    };

    class master_player_script : public PlayerScript
    {
    public:
        master_player_script() : PlayerScript("noble_next_master_player_script") { }

        void OnLogin(Player* player, bool /*firstLogin*/) override
        {
            if (player)
                NpcPoseService::Instance().ApplyNearby(player, 200.f);
        }

        void OnMapChanged(Player* player) override
        {
            if (player)
                NpcPoseService::Instance().ApplyNearby(player, 200.f);
        }
    };
}

void AddSC_NobleNextMasterScripts()
{
    new RoleplayCore::NobleNext::master_world_script();
    new RoleplayCore::NobleNext::master_player_script();
}
