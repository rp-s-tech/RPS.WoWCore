/*
 * NobleNext — GameObject soft groups WorldScript + post-save hook impl.
 */

#include "noble_next_gobgroup_hook.h"
#include "noble_next_gobgroup_mgr.h"

#include "ScriptMgr.h"

namespace RoleplayCore::NobleNext
{
    class noble_next_gobgroup_world_script : public WorldScript
    {
    public:
        noble_next_gobgroup_world_script() : WorldScript("noble_next_gobgroup_world_script") { }

        void OnStartup() override
        {
            sGobGroupMgr.LoadAndValidate();
        }

        void OnUpdate(uint32 diff) override
        {
            sGobGroupMgr.Update(diff);
        }
    };
}

void AddSC_NobleNextGobGroupScripts()
{
    NobleNext_RegisterGameObjectTransformSavedHook([](ObjectGuid::LowType spawnId)
    {
        RoleplayCore::NobleNext::GobGroupMgr::Instance().OnSingleObjectTransformSaved(spawnId);
    });
    NobleNext_RegisterGameObjectDeleteHooks(
        [](ObjectGuid::LowType spawnId, std::string& error)
        {
            return RoleplayCore::NobleNext::GobGroupMgr::Instance().CanDeleteGameObject(spawnId, error);
        },
        [](ObjectGuid::LowType spawnId)
        {
            RoleplayCore::NobleNext::GobGroupMgr::Instance().OnGameObjectDeleted(spawnId);
        });
    new RoleplayCore::NobleNext::noble_next_gobgroup_world_script();
}
