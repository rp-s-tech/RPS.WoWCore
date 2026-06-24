/*
 * NobleNext — .daytime / .setgrouptime commands.
 */

#include "noble_next_daytime.h"
#include "noble_next_staff.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Group.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class DaytimeCommands : public CommandScript
    {
    public:
        DaytimeCommands() : CommandScript("noble_next_daytime_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "daytime",       HandleDaytime,       rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "setgrouptime",  HandleSetGroupTime,  rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleDaytime(ChatHandler* handler, Optional<std::string> presetArg)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            if (!presetArg)
            {
                DaytimeMgr::Instance().SendHelp(player, false);
                return true;
            }

            if (*presetArg == "server")
            {
                DaytimeMgr::Instance().SendServerTime(player);
                handler->SendSysMessage("Серверное время возвращено.");
                return true;
            }

            std::optional<uint8> presetId = DaytimeMgr::ParsePresetId(*presetArg);
            if (!presetId || !DaytimeMgr::Instance().SendPreset(player, *presetId))
            {
                handler->SendSysMessage("Неверный ID времени (1-6) или server.");
                return false;
            }

            handler->SendSysMessage("Время установлено.");
            return true;
        }

        static bool HandleSetGroupTime(ChatHandler* handler, Optional<std::string> presetArg)
        {
            Player* player = handler->GetPlayer();
            if (!player || player->GetSession()->GetSecurity() <= SEC_PLAYER)
                return false;

            if (!presetArg)
            {
                DaytimeMgr::Instance().SendHelp(player, true);
                return true;
            }

            if (*presetArg == "server")
            {
                DaytimeMgr::Instance().SendServerTime(player);
                if (Group* group = player->GetGroup())
                    for (GroupReference const& ref : group->GetMembers())
                        if (Player* member = ref.GetSource())
                            DaytimeMgr::Instance().SendServerTime(member);

                handler->SendSysMessage("Серверное время возвращено.");
                return true;
            }

            std::optional<uint8> presetId = DaytimeMgr::ParsePresetId(*presetArg);
            if (!presetId || !DaytimeMgr::Instance().SendPreset(player, *presetId))
            {
                handler->SendSysMessage("Неверный ID времени (1-6) или server.");
                return false;
            }

            if (Group* group = player->GetGroup())
            {
                for (GroupReference const& ref : group->GetMembers())
                {
                    if (Player* member = ref.GetSource())
                        DaytimeMgr::Instance().SendPreset(member, *presetId);
                }
                handler->SendSysMessage("Время установлено для группы.");
            }
            else
                handler->SendSysMessage("Время установлено (группа не найдена).");

            return true;
        }
    };

    class DaytimePlayerScript : public PlayerScript
    {
    public:
        DaytimePlayerScript() : PlayerScript("noble_next_daytime_player_script") { }

        void OnUpdate(Player* player, uint32 diff) override
        {
            DaytimeMgr::Instance().Update(player, diff);
        }

        void OnLogout(Player* player) override
        {
            DaytimeMgr::Instance().OnLogout(player);
        }
    };
}

void AddSC_NobleNextDaytimeCommands()
{
    new RoleplayCore::NobleNext::DaytimeCommands();
    new RoleplayCore::NobleNext::DaytimePlayerScript();
}
