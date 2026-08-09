/*
 * NobleNext — gameobject teleport handler.
 */

#include "noble_next_gobtele_handler.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameObjectData.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "RoleplayPhaseMgr.h"
#include "ScriptMgr.h"
#include "WorldDatabase.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    GobTeleHandler& GobTeleHandler::Instance()
    {
        static GobTeleHandler instance;
        return instance;
    }

    void GobTeleHandler::RegisterGossipForEntry(uint32 entry)
    {
        if (!_gossipEntries.insert(entry).second)
            return;
        BindEntryScript(entry);
    }

    void GobTeleHandler::BindEntryScript(uint32 entry)
    {
        constexpr char SCRIPT_NAME[] = "script_noble_next_gobtele";
        uint32 scriptId = sObjectMgr->GetScriptId(SCRIPT_NAME);
        if (!scriptId)
            return;

        WorldDatabase.PExecute(
            "UPDATE gameobject_template SET ScriptName = '{}' WHERE entry = {}",
            SCRIPT_NAME, entry);

        GameObjectTemplateContainer& store =
            const_cast<GameObjectTemplateContainer&>(sObjectMgr->GetGameObjectTemplates());
        if (auto itr = store.find(entry); itr != store.end())
            itr->second.ScriptId = scriptId;
    }

    void GobTeleHandler::LoadGossipEntries()
    {
        _gossipEntries.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT DISTINCT g.entry FROM gameobject_teleport g "
            "WHERE g.map <> 2783 "
            "AND EXISTS (SELECT 1 FROM gameobject_template gt WHERE gt.entry = g.entry)");

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            RegisterGossipForEntry(fields[0].GetUInt32());
        } while (result->NextRow());
    }

    bool GobTeleHandler::TryTeleport(Player* player, uint32 goGuidLow)
    {
        if (!player)
            return false;

        QueryResult result = WorldDatabase.PQuery(
            "SELECT map, position_x, position_y, position_z, orientation, phase, server_phase_id "
            "FROM gameobject_teleport WHERE guid = {}", goGuidLow);

        if (!result)
            return false;

        Field* fields = result->Fetch();
        uint32 mapId = fields[0].GetUInt32();
        float x = fields[1].GetFloat();
        float y = fields[2].GetFloat();
        float z = fields[3].GetFloat();
        float o = fields[4].GetFloat();
        uint32 phase = fields[5].GetUInt32();
        Optional<uint64> serverPhaseId;
        if (!fields[6].IsNull())
            serverPhaseId = fields[6].GetUInt64();

        if (mapId == HousingMapId)
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "Общий GobTele не обслуживает Housing Map 2783: нужен PrivateHome record.");
            return false;
        }

        // Validate ACL and persist the logical context before moving the player.
        // A failed transition deliberately leaves the player at the source location.
        if (serverPhaseId && !sRoleplayPhaseMgr.TransitionPlayer(player, *serverPhaseId, mapId))
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "Телепорт отклонён: logical RP phase destination недоступна для персонажа.");
            return false;
        }

        player->TeleportTo(mapId, x, y, z, o);
        if (phase)
            PhasingHandler::AddPhase(player, phase, true);

        return true;
    }

    bool GobTeleHandler::SetTeleportDestination(Player* player, uint32 goGuidLow)
    {
        if (!player)
            return false;

        QueryResult goResult = WorldDatabase.PQuery(
            "SELECT id FROM gameobject WHERE guid = {}", goGuidLow);
        if (!goResult)
            return false;

        uint32 entry = (*goResult)[0].GetUInt32();

        float x = player->GetPositionX();
        float y = player->GetPositionY();
        float z = player->GetPositionZ();
        float o = player->GetOrientation();
        uint32 mapId = player->GetMapId();
        uint32 accountId = player->GetSession()->GetAccountId();
        uint64 const serverPhaseId = sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), mapId);
        std::string const serverPhaseSql = serverPhaseId ? fmt::format("{}", serverPhaseId) : "NULL";

        if (mapId == HousingMapId)
            return false;

        QueryResult exists = WorldDatabase.PQuery(
            "SELECT 1 FROM gameobject_teleport WHERE guid = {} LIMIT 1", goGuidLow);

        if (exists)
        {
            WorldDatabase.PExecute(
                "UPDATE gameobject_teleport SET map = {}, position_x = {}, position_y = {}, "
                "position_z = {}, orientation = {}, user = {}, phase = 0, server_phase_id = {} WHERE guid = {}",
                mapId, x, y, z, o, accountId, serverPhaseSql, goGuidLow);
            ChatHandler(player->GetSession()).SendSysMessage(fmt::format("Телепорт для объекта {} ОБНОВЛЁН.", goGuidLow));
        }
        else
        {
            WorldDatabase.PExecute(
                "INSERT INTO gameobject_teleport (guid, entry, map, position_x, position_y, position_z, "
                "orientation, user, phase, server_phase_id) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, 0, {})",
                goGuidLow, entry, mapId, x, y, z, o, accountId, serverPhaseSql);
            ChatHandler(player->GetSession()).SendSysMessage(fmt::format("Телепорт для объекта {} СОЗДАН.", goGuidLow));
        }

        RegisterGossipForEntry(entry);
        return true;
    }
}
