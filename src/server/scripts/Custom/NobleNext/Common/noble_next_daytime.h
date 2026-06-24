/*
 * NobleNext — client time-of-day presets (SMSG_LOGIN_SET_TIME_SPEED).
 */

#pragma once

#include "Define.h"
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class Player;

namespace RoleplayCore::NobleNext
{
    class DaytimeMgr
    {
    public:
        static DaytimeMgr& Instance();

        bool SendPreset(Player* player, uint8 presetId);
        void SendServerTime(Player* player);
        void SendHelp(Player* player, bool groupMode);
        void Update(Player* player, uint32 diff);
        void OnLogout(Player* player);

        static std::optional<uint8> ParsePresetId(std::string_view text);
        static constexpr uint8 PresetCount = 6;

    private:
        static bool BuildWowTimeFromMinutes(uint32 minutes, class WowTime& out);
        static bool BuildCurrentWowTime(class WowTime& out);
        void SendTimePacket(Player* player, uint32 minutes, float speed);
        void SendCurrentTimePacket(Player* player);

        struct LockedTime
        {
            uint32 Minutes = 0;
            uint32 ElapsedMs = 0;
        };

        std::unordered_map<std::string, LockedTime> _lockedTimes;
    };
}
