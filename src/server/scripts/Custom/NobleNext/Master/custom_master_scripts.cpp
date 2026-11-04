/*
 * NobleNext Master — startup / login hooks for saved NPC poses.
 */

#include "noble_next_npc_pose.h"

#include "Player.h"
#include "ScriptMgr.h"
#include "DB2Stores.h"
#include "Map.h"
#include <fmt/format.h>

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
        void OnUpdate(uint32 diff) override { NpcPoseService::Instance().Update(diff); }
    };

    class master_map_script : public WorldMapScript
    {
    public:
        explicit master_map_script(uint32 id) : WorldMapScript(fmt::format("noble_next_pose_map_{}", id).c_str(), id) { }
        void OnUpdate(Map* map, uint32 diff) override { NpcPoseService::Instance().UpdateMap(map, diff); }
        void OnDestroy(Map* map) override { NpcPoseService::Instance().ForgetMap(map); }
    };
}

void AddSC_NobleNextMasterScripts()
{
    new RoleplayCore::NobleNext::master_world_script();
    for (MapEntry const* entry : sMapStore)
        if (entry->IsWorldMap()) new RoleplayCore::NobleNext::master_map_script(entry->ID);
}
