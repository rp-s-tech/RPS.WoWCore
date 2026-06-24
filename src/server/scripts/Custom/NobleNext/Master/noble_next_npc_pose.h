#pragma once

#include "Define.h"
#include <unordered_map>

class ChatHandler;
class Creature;
class Player;

namespace RoleplayCore::NobleNext
{
    struct SavedNpcPose
    {
        uint32 Entry = 0;
        ObjectGuid::LowType SpawnId = 0;
        uint16 Animation = 0;
        float Size = 1.f;
        uint32 Mount = 0;
        uint8 Byte1 = 0;
        uint8 Byte2 = 0;
    };

    class NpcPoseService
    {
    public:
        static NpcPoseService& Instance();

        void LoadFromDatabase();
        bool SaveCreaturePose(Creature* creature, Player* saver, ChatHandler* handler);
        void ApplyIfSaved(Creature* creature) const;
        void ApplyNearby(Player* player, float range) const;

    private:
        NpcPoseService() = default;

        static void ApplyPose(Creature* creature, SavedNpcPose const& pose);

        std::unordered_map<ObjectGuid::LowType, SavedNpcPose> _poses;
    };
}
