/*
 * NobleNext — per-player local weather.
 */

#include "noble_next_local_weather.h"

#include "MiscPackets.h"
#include "Player.h"
#include "Weather.h"

namespace RoleplayCore::NobleNext
{
    LocalWeatherMgr& LocalWeatherMgr::Instance()
    {
        static LocalWeatherMgr instance;
        return instance;
    }

    float LocalWeatherMgr::NormalizeStrength(int32 strength)
    {
        if (strength <= 0)
            return 0.f;
        if (strength >= 10)
            return 1.f;
        return strength * 0.1f;
    }

    std::optional<uint32> LocalWeatherMgr::WeatherTypeFromIndex(uint8 index)
    {
        switch (index)
        {
            case 1: return 0;
            case 2: return 5;
            case 3: return 8;
            case 4: return 42;
            default: return std::nullopt;
        }
    }

    void LocalWeatherMgr::SendWeatherPacket(Player* player, uint32 weatherType, float intensity)
    {
        if (!player)
            return;

        WorldPackets::Misc::Weather weather(static_cast<WeatherState>(weatherType), intensity, true);
        player->SendDirectMessage(weather.Write());
    }

    void LocalWeatherMgr::ReapplyIfNeeded(Player* player)
    {
        if (!player)
            return;

        auto it = _playerWeather.find(player->GetName());
        if (it != _playerWeather.end())
        {
            SendWeatherPacket(player, it->second.Type, it->second.Intensity);
            return;
        }

        std::string zoneKey = std::to_string(player->GetZoneId());
        auto zoneIt = _savedZoneWeather.find(zoneKey);
        if (zoneIt != _savedZoneWeather.end())
            SendWeatherPacket(player, zoneIt->second.Type, zoneIt->second.Intensity);
        else
            SendWeatherPacket(player, 0, 0.f);
    }

    void LocalWeatherMgr::Apply(Player* player, uint32 weatherType, float intensity)
    {
        if (!player)
            return;

        if (weatherType == 0)
            intensity = 0.f;

        SendWeatherPacket(player, weatherType, intensity);
        _playerWeather[player->GetName()] = { weatherType, intensity };
    }

    void LocalWeatherMgr::Cancel(Player* player)
    {
        if (!player)
            return;

        _playerWeather.erase(player->GetName());

        std::string zoneKey = std::to_string(player->GetZoneId());
        auto zoneIt = _savedZoneWeather.find(zoneKey);
        if (zoneIt != _savedZoneWeather.end())
            SendWeatherPacket(player, zoneIt->second.Type, zoneIt->second.Intensity);
        else
            SendWeatherPacket(player, 0, 0.f);
    }

    void LocalWeatherMgr::OnUpdateZone(Player* player)
    {
        ReapplyIfNeeded(player);
    }

    void LocalWeatherMgr::OnLogout(Player* player)
    {
        if (!player)
            return;
        _playerWeather.erase(player->GetName());
    }

    void LocalWeatherMgr::OnZoneWeatherChange(uint32 zoneId, uint32 state, float grade)
    {
        _savedZoneWeather[std::to_string(zoneId)] = { state, grade };

        // Re-apply custom weather for online players in this zone
        // (players with personal override keep their setting via ReapplyIfNeeded)
    }
}
