/*
 * NobleNext — per-player local weather (SMSG_WEATHER).
 */

#pragma once

#include "Define.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

class Player;

namespace RoleplayCore::NobleNext
{
    struct LocalWeatherState
    {
        uint32 Type = 0;
        float Intensity = 0.f;
    };

    class LocalWeatherMgr
    {
    public:
        static LocalWeatherMgr& Instance();

        void Apply(Player* player, uint32 weatherType, float intensity);
        void Cancel(Player* player);
        void OnUpdateZone(Player* player);
        void OnLogout(Player* player);
        void OnZoneWeatherChange(uint32 zoneId, uint32 state, float grade);

        static float NormalizeStrength(int32 strength);
        static std::optional<uint32> WeatherTypeFromIndex(uint8 index);

    private:
        std::unordered_map<std::string, LocalWeatherState> _playerWeather;
        std::unordered_map<std::string, LocalWeatherState> _savedZoneWeather;

        void SendWeatherPacket(Player* player, uint32 weatherType, float intensity);
        void ReapplyIfNeeded(Player* player);
    };
}
