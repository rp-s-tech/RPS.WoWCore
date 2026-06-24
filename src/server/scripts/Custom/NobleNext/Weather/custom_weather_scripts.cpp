/*
 * NobleNext — weather player/world scripts.
 */

#include "noble_next_local_weather.h"

#include "Player.h"
#include "ScriptMgr.h"
#include "Weather.h"

namespace RoleplayCore::NobleNext
{
    class noble_next_weather_player_script : public PlayerScript
    {
    public:
        noble_next_weather_player_script() : PlayerScript("noble_next_weather_player_script") { }

        void OnUpdateZone(Player* player, uint32 /*newZone*/, uint32 /*newArea*/) override
        {
            LocalWeatherMgr::Instance().OnUpdateZone(player);
        }

        void OnLogout(Player* player) override
        {
            LocalWeatherMgr::Instance().OnLogout(player);
        }
    };

    class noble_next_weather_script : public WeatherScript
    {
    public:
        noble_next_weather_script() : WeatherScript("noble_next_weather_script") { }

        void OnChange(Weather* weather, WeatherState state, float grade) override
        {
            if (!weather)
                return;
            LocalWeatherMgr::Instance().OnZoneWeatherChange(weather->GetZone(), static_cast<uint32>(state), grade);
        }
    };
}

void AddSC_NobleNextWeatherScripts()
{
    new RoleplayCore::NobleNext::noble_next_weather_player_script();
    new RoleplayCore::NobleNext::noble_next_weather_script();
}
