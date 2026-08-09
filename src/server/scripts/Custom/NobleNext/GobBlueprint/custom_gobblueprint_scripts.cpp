/*
 * NobleNext — GobBlueprint WorldScript (async spawn job poll).
 */

#include "noble_next_gobblueprint_mgr.h"

#include "ScriptMgr.h"

namespace RoleplayCore::NobleNext
{
    class noble_next_gobblueprint_world_script : public WorldScript
    {
    public:
        noble_next_gobblueprint_world_script() : WorldScript("noble_next_gobblueprint_world_script") { }

        void OnUpdate(uint32 diff) override
        {
            sGobBlueprintMgr.Update(diff);
        }
    };
}

void AddSC_NobleNextGobBlueprintScripts()
{
    new RoleplayCore::NobleNext::noble_next_gobblueprint_world_script();
}
