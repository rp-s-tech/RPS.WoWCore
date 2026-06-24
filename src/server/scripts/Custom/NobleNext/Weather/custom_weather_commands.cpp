/*
 * NobleNext — .weather commands.
 */

#include "noble_next_local_weather.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Group.h"
#include "Player.h"
#include "RBAC.h"

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class WeatherCommands : public CommandScript
    {
    public:
        WeatherCommands() : CommandScript("noble_next_weather_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "weather", HandleWeather, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleWeather(ChatHandler* handler, Optional<std::string> arg1, Optional<int32> strength, Optional<PlayerIdentifier> targetName)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            if (!arg1)
            {
                handler->SendSysMessage("Использование: .weather <id> [0-10] [имя] | .weather cancel");
                handler->SendSysMessage("  1 — Солнце, 2 — Дождь, 3 — Снег, 4 — Буря");
                return true;
            }

            if (*arg1 == "cancel")
            {
                LocalWeatherMgr::Instance().Cancel(player);
                handler->SendSysMessage("Стандартная погода возвращена.");
                return true;
            }

            uint8 index = 0;
            try
            {
                size_t pos = 0;
                unsigned long parsed = std::stoul(*arg1, &pos);
                if (pos != arg1->size() || parsed < 1 || parsed > 4)
                    throw std::exception();
                index = static_cast<uint8>(parsed);
            }
            catch (...)
            {
                handler->SendSysMessage("Неверный ID погоды (1-4) или cancel.");
                return false;
            }

            auto weatherType = LocalWeatherMgr::WeatherTypeFromIndex(index);
            if (!weatherType)
            {
                handler->SendSysMessage("Неверный ID погоды.");
                return false;
            }

            float intensity = LocalWeatherMgr::NormalizeStrength(strength.value_or(5));

            if (targetName)
            {
                Player* target = targetName->GetConnectedPlayer();
                if (!target)
                {
                    handler->SendSysMessage("Игрок не найден.");
                    return false;
                }
                LocalWeatherMgr::Instance().Apply(target, *weatherType, intensity);
                handler->SendSysMessage("Погода установлена.");
                return true;
            }

            if (player->GetGroup())
            {
                Group* group = player->GetGroup();
                for (GroupReference const& ref : group->GetMembers())
                {
                    if (Player* member = ref.GetSource())
                        LocalWeatherMgr::Instance().Apply(member, *weatherType, intensity);
                }
                handler->SendSysMessage("Погода установлена группе.");
                return true;
            }

            LocalWeatherMgr::Instance().Apply(player, *weatherType, intensity);
            handler->SendSysMessage("Погода установлена.");
            return true;
        }
    };
}

void AddSC_NobleNextWeatherCommands()
{
    new RoleplayCore::NobleNext::WeatherCommands();
}
