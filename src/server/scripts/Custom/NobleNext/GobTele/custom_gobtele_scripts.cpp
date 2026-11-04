/*
 * NobleNext — gobtele gossip + startup.
 */

#include "noble_next_gobtele_handler.h"

#include "GameObject.h"
#include "GameObjectAI.h"
#include "Player.h"
#include "ScriptMgr.h"

namespace RoleplayCore::NobleNext
{
    class script_noble_next_gobtele : public GameObjectScript
    {
    public:
        script_noble_next_gobtele() : GameObjectScript("script_noble_next_gobtele") { }

        struct TeleportAI : GameObjectAI
        {
            explicit TeleportAI(GameObject* go) : GameObjectAI(go) { }
            bool OnGossipHello(Player* player) override
            { return GobTeleHandler::Instance().TryTeleport(player, me->GetSpawnId()); }
        };

        GameObjectAI* GetAI(GameObject* go) const override { return new TeleportAI(go); }

        bool OnGossipHello(Player* player, GameObject* go) override
        {
            if (!player || !go)
                return false;

            if (GobTeleHandler::Instance().TryTeleport(player, go->GetSpawnId()))
                return true;

            return false;
        }
    };

    class noble_next_gobtele_world_script : public WorldScript
    {
    public:
        noble_next_gobtele_world_script() : WorldScript("noble_next_gobtele_world_script") { }

        void OnStartup() override
        {
            GobTeleHandler::Instance().LoadGossipEntries();
        }
        void OnUpdate(uint32 /*diff*/) override { GobTeleHandler::Instance().Update(); }
    };
}

void AddSC_NobleNextGobTeleScripts()
{
    new RoleplayCore::NobleNext::script_noble_next_gobtele();
    new RoleplayCore::NobleNext::noble_next_gobtele_world_script();
}
