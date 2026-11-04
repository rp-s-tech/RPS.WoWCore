/*
 * NobleNext — saved NPC pose (legacy saved_npc).
 */

#include "noble_next_npc_pose.h"

#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayPhaseMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Unit.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    namespace
    {
        AsyncCallbackProcessor<TransactionCallback> Commits;
    }
    NpcPoseService& NpcPoseService::Instance()
    {
        static NpcPoseService instance;
        return instance;
    }

    void NpcPoseService::LoadFromDatabase()
    {
        _poses.clear();

        QueryResult result = WorldDatabase.Query(
            "SELECT `entry`, `guid`, `animation`, `size`, `mount`, `byte1`, `byte2` FROM `saved_npc`");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            SavedNpcPose pose;
            pose.Entry = fields[0].GetUInt32();
            pose.SpawnId = fields[1].GetUInt32();
            pose.Animation = fields[2].GetUInt16();
            pose.Size = fields[3].GetFloat();
            pose.Mount = fields[4].GetUInt32();
            pose.Byte1 = fields[5].GetUInt8();
            pose.Byte2 = fields[6].GetUInt8();
            _poses.emplace(pose.SpawnId, pose);
        } while (result->NextRow());
    }

    void NpcPoseService::ApplyPose(Creature* creature, SavedNpcPose const& pose)
    {
        if (!creature)
            return;

        creature->SetEmoteState(static_cast<Emote>(pose.Animation));
        // Size belongs to creature.size; replaying a pose must not undo .npc set scale.

        if (pose.Mount)
            creature->Mount(pose.Mount);
        else if (creature->IsMounted())
            creature->Dismount();

        creature->SetStandState(static_cast<UnitStandStateType>(pose.Byte1));
        creature->SetSheath(static_cast<SheathState>(pose.Byte2));
    }

    void NpcPoseService::Update(uint32 /*diff*/)
    {
        Commits.ProcessReadyCallbacks();
    }

    void NpcPoseService::ForgetMap(Map* map)
    {
        std::lock_guard lock(_mutex);
        _applied.erase(map->GetId());
        _timers.erase(map->GetId());
    }

    void NpcPoseService::UpdateMap(Map* map, uint32 diff)
    {
        std::lock_guard lock(_mutex);
        uint32& timer = _timers[map->GetId()];
        if (timer > diff) { timer -= diff; return; }
        timer = 1000;
        auto& applied = _applied[map->GetId()];
        for (auto const& [spawn, pose] : _poses)
        {
            CreatureData const* data = sObjectMgr->GetCreatureData(spawn);
            if (!data || data->mapId != map->GetId() || data->id != pose.Entry) continue;
            Creature* creature = map->GetCreatureBySpawnId(spawn);
            if (!creature || !creature->IsAlive()) { applied.erase(spawn); continue; }
            if (creature->IsInCombat() || creature->IsCharmed() || _pending.contains(spawn)) continue;
            if (applied[spawn] == creature->GetGUID()) continue;
            ApplyPose(creature, pose);
            applied[spawn] = creature->GetGUID();
        }
    }

    bool NpcPoseService::SaveCreaturePose(Creature* creature, Player* saver, ChatHandler* handler)
    {
        if (!creature || !saver || !creature->GetSpawnId() || creature->IsPet()
            || !creature->GetMap()->IsWorldMap() || creature->GetInstanceId())
        {
            if (handler) handler->SendSysMessage("Выберите постоянного НИП на общей карте; временного сначала запеките.");
            return false;
        }
        if (!RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(saver, handler,
            RoleplayPhaseSpawnType::Creature, creature->GetSpawnId(), false, "npc_pose_save")) return false;
        std::lock_guard lock(_mutex);
        if (!_pending.insert(creature->GetSpawnId()).second)
        {
            if (handler) handler->SendSysMessage("Дождитесь завершения сохранения позы этого НИП.");
            return false;
        }

        SavedNpcPose pose;
        pose.Entry = creature->GetEntry();
        pose.SpawnId = creature->GetSpawnId();
        pose.Animation = creature->GetEmoteState();
        pose.Size = creature->GetObjectScale();
        pose.Mount = creature->IsMounted() ? creature->GetMountDisplayId() : 0;
        pose.Byte1 = static_cast<uint8>(creature->GetStandState());
        pose.Byte2 = static_cast<uint8>(creature->GetSheath());

        uint32 accountId = saver->GetSession()->GetAccountId();
        auto transaction = WorldDatabase.BeginTransaction();
        transaction->PAppend("UPDATE creature SET size = {} WHERE guid = {} AND id = {}", pose.Size, pose.SpawnId, pose.Entry);
        transaction->PAppend(
            "REPLACE INTO `saved_npc` (`entry`, `guid`, `animation`, `size`, `mount`, `byte1`, `byte2`, `auras`, `gm_account_id`) "
            "SELECT id, guid, {}, {}, {}, {}, {}, '0', {} FROM creature WHERE guid = {} AND id = {}",
            pose.Animation, pose.Size, pose.Mount, pose.Byte1, pose.Byte2, accountId, pose.SpawnId, pose.Entry);

        ObjectGuid author = saver->GetGUID();
        ObjectGuid liveGuid = creature->GetGUID();
        uint32 mapId = creature->GetMapId();
        float previousSize = creature->GetCreatureData()->size;
        auto callback = WorldDatabase.AsyncCommitTransaction(transaction);
        callback.AfterComplete([this, pose, author, liveGuid, mapId, previousSize](bool success)
        {
            CreatureData const* savedData = sObjectMgr->GetCreatureData(pose.SpawnId);
            success = success && savedData && savedData->id == pose.Entry;
            {
                std::lock_guard callbackLock(_mutex);
                _pending.erase(pose.SpawnId);
                if (success)
                {
                    _poses[pose.SpawnId] = pose;
                    _applied[mapId][pose.SpawnId] = liveGuid;
                    if (CreatureData const* data = sObjectMgr->GetCreatureData(pose.SpawnId);
                        data && data->id == pose.Entry && data->size == previousSize)
                        const_cast<CreatureData*>(data)->size = pose.Size;
                }
            }
            if (Player* player = ObjectAccessor::FindConnectedPlayer(author))
                ChatHandler(player->GetSession()).SendSysMessage(success ? "Поза НИП сохранена." : "Ошибка БД: поза НИП не сохранена.");
        });
        Commits.AddCallback(std::move(callback));

        if (handler)
            handler->SendSysMessage("Поза сохраняется; дождитесь подтверждения.");

        return true;
    }
}
