#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

class ChatHandler;
class Creature;
class Player;
class Map;

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
        void Update(uint32 diff);
        void UpdateMap(Map* map, uint32 diff);
        void ForgetMap(Map* map);

    private:
        NpcPoseService() = default;

        static void ApplyPose(Creature* creature, SavedNpcPose const& pose);

        std::unordered_map<ObjectGuid::LowType, SavedNpcPose> _poses;
        std::set<ObjectGuid::LowType> _pending;
        std::map<uint32, std::map<ObjectGuid::LowType, ObjectGuid>> _applied;
        std::map<uint32, uint32> _timers;
        std::mutex _mutex;
    };
}
