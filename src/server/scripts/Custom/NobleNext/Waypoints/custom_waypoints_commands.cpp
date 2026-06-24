/*
 * NobleNext — waypoint GM commands.
 */

#include "noble_next_staff.h"
#include "noble_next_waypoints_service.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Creature.h"
#include "Player.h"
#include "RBAC.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class WaypointsCommands : public CommandScript
    {
    public:
        WaypointsCommands() : CommandScript("noble_next_waypoints_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "wpmove",     HandleWpMove,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpwalk",     HandleWpWalk,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpwait",     HandleWpWait,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpemote",    HandleWpEmote,    rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpclear",    HandleWpClear,    rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpgo",       HandleWpGo,       rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpstop",     HandleWpStop,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "npcsetstate", HandleNpcSetState, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static Creature* GetOwnedTarget(ChatHandler* handler, Player* player)
        {
            Creature* c = handler->getSelectedCreature();
            if (!c)
            {
                handler->SendSysMessage("Выберите NPC.");
                return nullptr;
            }
            if (!CanControlOwnedCreature(player, c) && player->GetSession()->GetSecurity() <= SEC_PLAYER)
            {
                handler->SendSysMessage("Нет прав на этого NPC.");
                return nullptr;
            }
            return c;
        }

        static bool HandleWpMove(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().AddMovePoint(c, p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), false);
            h->SendSysMessage(fmt::format("Точка бега добавлена ({})", WaypointsService::Instance().PointCount(c)));
            return true;
        }

        static bool HandleWpWalk(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().AddMovePoint(c, p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), true);
            h->SendSysMessage("Точка шага добавлена.");
            return true;
        }

        static bool HandleWpWait(ChatHandler* h, float seconds)
        {
            if (seconds < 0.5f) { h->SendSysMessage("Минимум 0.5 сек."); return false; }
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().AddWaitPoint(c, seconds, 0);
            h->SendSysMessage("Точка ожидания добавлена.");
            return true;
        }

        static bool HandleWpEmote(ChatHandler* h, uint32 emoteId, float seconds)
        {
            if (seconds < 0.5f) { h->SendSysMessage("Минимум 0.5 сек."); return false; }
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().AddWaitPoint(c, seconds, emoteId);
            h->SendSysMessage("Точка эмоции добавлена.");
            return true;
        }

        static bool HandleWpClear(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().ClearRoute(c);
            h->SendSysMessage("Маршрут очищен.");
            return true;
        }

        static bool HandleWpGo(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().StartRoute(c);
            h->SendSysMessage("Маршрут запущен.");
            return true;
        }

        static bool HandleWpStop(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().StopRoute(c);
            h->SendSysMessage("Маршрут остановлен.");
            return true;
        }

        static bool HandleNpcSetState(ChatHandler* h, uint32 standState)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            WaypointsService::Instance().SetStandState(c, standState);
            h->SendSysMessage("Стойка NPC установлена.");
            return true;
        }
    };
}

void AddSC_NobleNextWaypointsCommands()
{
    new RoleplayCore::NobleNext::WaypointsCommands();
}
