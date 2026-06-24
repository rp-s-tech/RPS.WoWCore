/*
 * NobleNext — saved NPC pose (legacy saved_npc).
 */

#include "noble_next_npc_pose.h"

#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Unit.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
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
        creature->SetObjectScale(pose.Size);

        if (pose.Mount)
            creature->Mount(pose.Mount);
        else if (creature->IsMounted())
            creature->Dismount();

        creature->SetStandState(static_cast<UnitStandStateType>(pose.Byte1));
        creature->SetSheath(static_cast<SheathState>(pose.Byte2));
    }

    void NpcPoseService::ApplyIfSaved(Creature* creature) const
    {
        if (!creature || !creature->GetSpawnId())
            return;

        auto itr = _poses.find(creature->GetSpawnId());
        if (itr == _poses.end())
            return;

        ApplyPose(creature, itr->second);
    }

    void NpcPoseService::ApplyNearby(Player* player, float range) const
    {
        if (!player || _poses.empty())
            return;

        std::list<Creature*> creatures;
        Trinity::AllCreaturesInRange check(player, range);
        Trinity::CreatureListSearcher<Trinity::AllCreaturesInRange> searcher(player, creatures, check);
        Cell::VisitGridObjects(player, searcher, range);

        for (Creature* creature : creatures)
            ApplyIfSaved(creature);
    }

    bool NpcPoseService::SaveCreaturePose(Creature* creature, Player* saver, ChatHandler* handler)
    {
        if (!creature || !saver || !creature->GetSpawnId())
            return false;

        SavedNpcPose pose;
        pose.Entry = creature->GetEntry();
        pose.SpawnId = creature->GetSpawnId();
        pose.Animation = creature->GetEmoteState();
        pose.Size = creature->GetObjectScale();
        pose.Mount = creature->IsMounted() ? creature->GetMountDisplayId() : 0;
        pose.Byte1 = static_cast<uint8>(creature->GetStandState());
        pose.Byte2 = static_cast<uint8>(creature->GetSheath());

        _poses[pose.SpawnId] = pose;
        ApplyPose(creature, pose);

        uint32 accountId = saver->GetSession()->GetAccountId();
        WorldDatabase.Execute(fmt::format(
            "REPLACE INTO `saved_npc` (`entry`, `guid`, `animation`, `size`, `mount`, `byte1`, `byte2`, `auras`, `gm_account_id`) "
            "VALUES ({}, {}, {}, {}, {}, {}, {}, '0', {})",
            pose.Entry, pose.SpawnId, pose.Animation, pose.Size, pose.Mount, pose.Byte1, pose.Byte2, accountId).c_str());

        if (handler)
            handler->SendSysMessage("Поза NPC сохранена.");

        return true;
    }
}
