/*
 * NobleNext — gameobject teleport handler.
 */

#include "noble_next_gobtele_handler.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "GameObjectData.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "RoleplayCommandPhaseGuard.h"
#include "ObjectMgr.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "RoleplayPhaseMgr.h"
#include "ScriptMgr.h"
#include "WorldDatabase.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    namespace { AsyncCallbackProcessor<TransactionCallback> Commits; }

    void GobTeleHandler::Update() { Commits.ProcessReadyCallbacks(); }
    GobTeleHandler& GobTeleHandler::Instance()
    {
        static GobTeleHandler instance;
        return instance;
    }

    void GobTeleHandler::RegisterGossipForEntry(uint32 entry)
    {
        if (BindEntryScript(entry)) _gossipEntries.insert(entry);
    }

    bool GobTeleHandler::BindEntryScript(uint32 entry)
    {
        constexpr char SCRIPT_NAME[] = "script_noble_next_gobtele";
        uint32 scriptId = sObjectMgr->GetScriptId(SCRIPT_NAME);
        if (!scriptId)
            return false;

        GameObjectTemplateContainer& store =
            const_cast<GameObjectTemplateContainer&>(sObjectMgr->GetGameObjectTemplates());
        auto itr = store.find(entry);
        if (itr == store.end() || (itr->second.ScriptId && itr->second.ScriptId != scriptId)
            || !itr->second.AIName.empty()) return false;
        itr->second.ScriptId = scriptId;
        // Existing spawns retain their AI object until explicitly reinitialized.
        sMapMgr->DoForAllMaps([entry, scriptId](Map* map)
        {
            for (auto const& [spawn, go] : map->GetGameObjectBySpawnIdStore())
                if (go->GetEntry() == entry && (!go->AI() || go->AI()->GetId() != scriptId))
                    go->AIM_Initialize();
        });
        return true;
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
        if (!player) return false;
        ChatHandler handler(player->GetSession());
        auto fail = [&handler](char const* text) { handler.SendSysMessage(text); handler.SetSentErrorMessage(true); return false; };
        GameObjectData const* spawn = sObjectMgr->GetGameObjectData(goGuidLow);
        if (!spawn) return fail("Серверный объект с таким GUID не найден.");
        if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, &handler,
            RoleplayPhaseSpawnType::GameObject, goGuidLow, false, "gobject_teleport_set")) return false;
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(spawn->id);
        uint32 const scriptId = sObjectMgr->GetScriptId("script_noble_next_gobtele");
        if (!info || !scriptId) return fail("Обработчик телепорта недоступен.");
        if ((info->ScriptId && info->ScriptId != scriptId) || !info->AIName.empty())
            return fail("У шаблона объекта уже есть другой сценарий. Используйте отдельную дверь без сценария.");
        if (info->type != GAMEOBJECT_TYPE_DOOR && info->type != GAMEOBJECT_TYPE_BUTTON
            && info->type != GAMEOBJECT_TYPE_GOOBER && info->type != GAMEOBJECT_TYPE_QUESTGIVER)
            return fail("Нужен объект с взаимодействием: дверь, кнопка, активируемый объект или источник заданий.");
        if (player->GetMapId() == HousingMapId || player->GetInstanceId())
            return fail("Для этой карты нельзя создать общий телепорт.");
        if (!_pending.insert(goGuidLow).second) return fail("Дождитесь завершения сохранения этого телепорта.");

        uint32 const entry = spawn->id;
        uint32 const mapId = player->GetMapId();
        uint64 const phase = sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), mapId);
        auto transaction = WorldDatabase.BeginTransaction();
        transaction->PAppend("INSERT INTO gameobject_teleport (guid, entry, map, position_x, position_y, position_z, orientation, user, phase, server_phase_id) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, 0, {}) ON DUPLICATE KEY UPDATE entry=VALUES(entry), map=VALUES(map), "
            "position_x=VALUES(position_x), position_y=VALUES(position_y), position_z=VALUES(position_z), orientation=VALUES(orientation), "
            "user=VALUES(user), phase=0, server_phase_id=VALUES(server_phase_id)",
            goGuidLow, entry, mapId, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
            player->GetOrientation(), player->GetSession()->GetAccountId(), phase);
        transaction->PAppend("UPDATE gameobject_template SET ScriptName='script_noble_next_gobtele' "
            "WHERE entry={} AND ScriptName IN ('', 'script_noble_next_gobtele')", entry);
        ObjectGuid const author = player->GetGUID();
        auto callback = WorldDatabase.AsyncCommitTransaction(transaction);
        callback.AfterComplete([this, goGuidLow, entry, author](bool success)
        {
            _pending.erase(goGuidLow);
            if (success) RegisterGossipForEntry(entry);
            if (Player* owner = ObjectAccessor::FindConnectedPlayer(author))
                ChatHandler(owner->GetSession()).SendSysMessage(success
                    ? "Телепорт сохранён. Загруженные двери обновлены."
                    : "Ошибка БД: телепорт не сохранён.");
        });
        Commits.AddCallback(std::move(callback));
        handler.SendSysMessage("Телепорт сохраняется; дождитесь подтверждения.");
        return true;
    }
}
