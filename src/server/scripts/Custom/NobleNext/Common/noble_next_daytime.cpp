/*
 * NobleNext — client time-of-day presets.
 */

#include "noble_next_daytime.h"

#include "Group.h"
#include "Chat.h"
#include "MiscPackets.h"
#include "Player.h"
#include "WowTime.h"

#include <ctime>
#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    namespace
    {
        struct TimePreset
        {
            uint32 Minutes;
            char const* Label;
        };

        constexpr TimePreset PRESETS[] =
        {
            { 300,  "Раннее утро" },
            { 550,  "Утро" },
            { 800,  "Полдень" },
            { 1160, "Вечер" },
            { 1310, "Поздний вечер" },
            { 0,    "Ночь" },
        };
    }

    DaytimeMgr& DaytimeMgr::Instance()
    {
        static DaytimeMgr instance;
        return instance;
    }

    bool DaytimeMgr::BuildWowTimeFromMinutes(uint32 minutes, WowTime& out)
    {
        std::time_t now = std::time(nullptr);
        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        uint32 hour = minutes / 60;
        uint32 minute = minutes % 60;

        out.SetYear((localTime.tm_year + 1900) % 100);
        out.SetMonth(static_cast<int8>(localTime.tm_mon));
        out.SetMonthDay(static_cast<int8>(localTime.tm_mday));
        out.SetWeekDay(static_cast<int8>(localTime.tm_wday));
        out.SetHour(static_cast<int8>(hour));
        out.SetMinute(static_cast<int8>(minute));
        out.SetFlags(0);
        return true;
    }

    bool DaytimeMgr::BuildCurrentWowTime(WowTime& out)
    {
        std::time_t now = std::time(nullptr);
        std::tm localTime;
#ifdef _WIN32
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif

        out.SetYear((localTime.tm_year + 1900) % 100);
        out.SetMonth(static_cast<int8>(localTime.tm_mon));
        out.SetMonthDay(static_cast<int8>(localTime.tm_mday));
        out.SetWeekDay(static_cast<int8>(localTime.tm_wday));
        out.SetHour(static_cast<int8>(localTime.tm_hour));
        out.SetMinute(static_cast<int8>(localTime.tm_min));
        out.SetFlags(0);
        return true;
    }

    void DaytimeMgr::SendTimePacket(Player* player, uint32 minutes, float speed)
    {
        if (!player)
            return;

        WowTime gameTime;
        BuildWowTimeFromMinutes(minutes, gameTime);

        WorldPackets::Misc::LoginSetTimeSpeed packet;
        packet.ServerTime = gameTime;
        packet.GameTime = gameTime;
        packet.NewSpeed = speed;
        packet.ServerTimeHolidayOffset = 0;
        packet.GameTimeHolidayOffset = 0;
        player->SendDirectMessage(packet.Write());
    }

    void DaytimeMgr::SendCurrentTimePacket(Player* player)
    {
        if (!player)
            return;

        WowTime gameTime;
        BuildCurrentWowTime(gameTime);

        WorldPackets::Misc::LoginSetTimeSpeed packet;
        packet.ServerTime = gameTime;
        packet.GameTime = gameTime;
        packet.NewSpeed = 0.017f;
        packet.ServerTimeHolidayOffset = 0;
        packet.GameTimeHolidayOffset = 0;
        player->SendDirectMessage(packet.Write());
    }

    std::optional<uint8> DaytimeMgr::ParsePresetId(std::string_view text)
    {
        try
        {
            size_t pos = 0;
            unsigned long id = std::stoul(std::string(text), &pos);
            if (pos != text.size() || id < 1 || id > PresetCount)
                return std::nullopt;
            return static_cast<uint8>(id);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool DaytimeMgr::SendPreset(Player* player, uint8 presetId)
    {
        if (!player || presetId < 1 || presetId > PresetCount)
            return false;
        uint32 minutes = PRESETS[presetId - 1].Minutes;
        SendTimePacket(player, minutes, 0.0f);
        _lockedTimes[player->GetName()] = { minutes, 0 };
        return true;
    }

    void DaytimeMgr::SendServerTime(Player* player)
    {
        if (!player)
            return;

        _lockedTimes.erase(player->GetName());
        SendCurrentTimePacket(player);
    }

    void DaytimeMgr::Update(Player* player, uint32 diff)
    {
        if (!player)
            return;

        auto it = _lockedTimes.find(player->GetName());
        if (it == _lockedTimes.end())
            return;

        it->second.ElapsedMs += diff;
        if (it->second.ElapsedMs < 10000)
            return;

        it->second.ElapsedMs = 0;
        SendTimePacket(player, it->second.Minutes, 0.0f);
    }

    void DaytimeMgr::OnLogout(Player* player)
    {
        if (player)
            _lockedTimes.erase(player->GetName());
    }

    void DaytimeMgr::SendHelp(Player* player, bool groupMode)
    {
        if (!player)
            return;

        char const* header = groupMode
            ? ".setgrouptime <id> — время для группы. ID:"
            : ".daytime <id> — время для себя. ID:";

        ChatHandler handler(player->GetSession());
        handler.SendSysMessage(header);
        for (uint8 i = 0; i < PresetCount; ++i)
            handler.SendSysMessage(fmt::format("  {} — {}", i + 1, PRESETS[i].Label));
    }
}
