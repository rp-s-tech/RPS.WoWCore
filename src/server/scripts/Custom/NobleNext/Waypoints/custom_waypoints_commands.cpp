/*
 * NobleNext — waypoint GM commands.
 */

#include "noble_next_staff.h"
#include "noble_next_waypoints_service.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "ChatPackets.h"
#include "Creature.h"
#include "Player.h"
#include "RBAC.h"
#include "DB2Stores.h"
#include "Map.h"
#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayVisibilityContext.h"
#include "WorldSession.h"

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
                { "wplist",     HandleWpList,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpremove",   HandleWpRemove,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "wpswap",     HandleWpSwap,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "npcsetstate", HandleNpcSetState, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static Creature* GetOwnedTarget(ChatHandler* handler, Player* player, bool readOnly = false)
        {
            Creature* c = handler->getSelectedCreature();
            if (!c)
            {
                handler->SendSysMessage("Выберите NPC.");
                return nullptr;
            }
            if (!readOnly && WaypointsService::Instance().IsLocked(c))
            {
                handler->SendSysMessage("Дождитесь завершения сохранения NPC или маршрута.");
                return nullptr;
            }
            if (!CanControlOwnedCreature(player, c) && player->GetSession()->GetSecurity() <= SEC_PLAYER)
            {
                handler->SendSysMessage("Нет прав на этого NPC.");
                return nullptr;
            }
            if (!player->GetPhaseShift().CanSee(c->GetPhaseShift())
                || (!RoleplayVisibilityContext::CanShare(*player, *c) && !CanControlOwnedCreature(player, c)))
            {
                handler->SendSysMessage("NPC находится в другом контексте фазы.");
                return nullptr;
            }
            if (c->GetSpawnId() && !RoleplayCommandPhaseGuard::AllowsViewerSpawnMutation(player, handler,
                RoleplayPhaseSpawnType::Creature, c->GetSpawnId(), false, "waypoint_edit")) return nullptr;
            return c;
        }

        static void SendRoutePacket(ChatHandler* h, std::string const& body)
        {
            WorldPackets::Chat::Chat packet;
            packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, h->GetPlayer(), h->GetPlayer(), body, 0, "", DEFAULT_LOCALE, "NN_WAYPOINTS");
            h->GetPlayer()->SendDirectMessage(packet.Write());
        }

        static bool RouteError(ChatHandler* h, uint32 request, std::string_view reason)
        {
            if (request) SendRoutePacket(h, fmt::format("ERROR|{}|{}", request, reason));
            h->SendSysMessage("Точки не изменены. Проверьте выделение, права и обновите список маршрута.");
            h->SetSentErrorMessage(true);
            return false;
        }

        static bool MatchesTarget(Creature* c, Optional<std::string> const& guid)
        {
            return c && (!guid || c->GetGUID() == ObjectGuid::FromString(*guid));
        }

        static void SendRoute(ChatHandler* h, Creature* c, uint32 page, uint32 request, Optional<uint32> requestedPageSize)
        {
            constexpr uint32 MaxPageSize = 50;
            // Older addon clients omit the size and still expect eight points.
            uint32 pageSize = std::clamp<uint32>(requestedPageSize.value_or(request ? 8 : MaxPageSize), 1, MaxPageSize);
            auto view = WaypointsService::Instance().ReadRoute(c);
            size_t total = view.Route.Points.size();
            uint32 pages = std::max<uint32>(1, uint32((total + pageSize - 1) / pageSize));
            page = std::clamp<uint32>(page, 1, pages);
            std::string_view status = view.Locked ? "saving" : view.Blocked ? "blocked" : view.Running ? "running" : "stopped";
            if (request)
            {
                SendRoutePacket(h, fmt::format("PAGE|{}|{}|{}|{}|{}|{}|{}", request, c->GetGUID().ToString(),
                    view.Route.Revision, total, page, view.Current, status));
                SendRoutePacket(h, fmt::format("SOURCE|{}|{}|{}|{}", request, view.Route.Revision, view.Native ? "native" : "draft", view.Route.PathId));
            }
            else
            {
                h->SendSysMessage(fmt::format("Маршрут: {} действий, страница {}/{}, текущее действие {} ({}).", total, page, pages, view.Current, status));
                if (view.Native) h->SendSysMessage(fmt::format("Штатный маршрут {}: только просмотр; исполнением управляет ядро/ИИ.", view.Route.PathId));
            }
            size_t first = size_t(page - 1) * pageSize;
            for (size_t i = first; i < std::min(first + pageSize, total); ++i)
            {
                auto const& p = view.Route.Points[i];
                if (request)
                    SendRoutePacket(h, fmt::format("POINT|{}|{}|{}|{}|{:.4f}|{:.4f}|{:.4f}|{}|{}",
                        request, view.Route.Revision, i + 1, p.Type, p.X, p.Y, p.Z, p.WaitMs, p.EmoteId));
                else
                    h->SendSysMessage(fmt::format("{}. {}: X {:.3f}, Y {:.3f}, Z {:.3f}; пауза {} мс, эмоция {}.",
                        i + 1, p.Type == 1 ? "бег" : p.Type == 3 ? "шаг" : p.Type == 4 ? "посадка" : p.Type == 5 ? "взлёт" : "ожидание", p.X, p.Y, p.Z, p.WaitMs, p.EmoteId));
            }
            if (request) SendRoutePacket(h, fmt::format("END|{}|{}", request, view.Route.Revision));
        }

        static bool HandleWpList(ChatHandler* h, Optional<uint32> page, Optional<uint32> request,
            Optional<std::string> expected, Optional<uint32> pageSize)
        {
            Creature* c = GetOwnedTarget(h, h->GetPlayer(), true);
            if (!MatchesTarget(c, expected)) return RouteError(h, request.value_or(0), "target");
            SendRoute(h, c, page.value_or(1), request.value_or(0), pageSize);
            return true;
        }

        static bool HandleWpRemove(ChatHandler* h, uint32 point, Optional<uint32> revision, Optional<std::string> expected,
            Optional<uint32> page, Optional<uint32> request, Optional<uint32> pageSize)
        {
            Creature* c = GetOwnedTarget(h, h->GetPlayer());
            if (!MatchesTarget(c, expected) || !WaypointsService::Instance().RemovePoint(c, point, revision.value_or(0)))
                return RouteError(h, request.value_or(0), "stale");
            h->SendSysMessage("Действие удалено из черновика. Для сохранения запеките маршрут.");
            SendRoute(h, c, page.value_or(1), request.value_or(0), pageSize);
            return true;
        }

        static bool HandleWpSwap(ChatHandler* h, uint32 first, uint32 second, Optional<uint32> revision, Optional<std::string> expected,
            Optional<uint32> page, Optional<uint32> request, Optional<uint32> pageSize)
        {
            Creature* c = GetOwnedTarget(h, h->GetPlayer());
            if (!MatchesTarget(c, expected) || !WaypointsService::Instance().SwapPoints(c, first, second, revision.value_or(0)))
                return RouteError(h, request.value_or(0), "stale");
            h->SendSysMessage("Действия поменялись местами в черновике. Для сохранения запеките маршрут.");
            SendRoute(h, c, page.value_or(1), request.value_or(0), pageSize);
            return true;
        }

        static bool HandleWpMove(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            if (!WaypointsService::Instance().AddMovePoint(c, p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), false)) return false;
            h->SendSysMessage(fmt::format("Точка бега добавлена ({})", WaypointsService::Instance().PointCount(c)));
            return true;
        }

        static bool HandleWpWalk(ChatHandler* h)
        {
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            if (!WaypointsService::Instance().AddMovePoint(c, p->GetPositionX(), p->GetPositionY(), p->GetPositionZ(), true)) return false;
            h->SendSysMessage("Точка шага добавлена.");
            return true;
        }

        static bool HandleWpWait(ChatHandler* h, float seconds)
        {
            if (seconds < 0.5f) { h->SendSysMessage("Минимум 0.5 сек."); return false; }
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            if (!WaypointsService::Instance().AddWaitPoint(c, seconds, 0)) return false;
            h->SendSysMessage("Точка ожидания добавлена.");
            return true;
        }

        static bool HandleWpEmote(ChatHandler* h, uint32 emoteId, float seconds)
        {
            if (seconds < 0.5f) { h->SendSysMessage("Минимум 0.5 сек."); return false; }
            Player* p = h->GetPlayer();
            Creature* c = GetOwnedTarget(h, p);
            if (!c) return false;
            if (!WaypointsService::Instance().AddWaitPoint(c, seconds, emoteId)) return false;
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
            if (!WaypointsService::Instance().StartRoute(c))
            {
                h->SendSysMessage("Маршрут пуст или сейчас сохраняется.");
                return false;
            }
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

    class WaypointsMapScript : public WorldMapScript
    {
    public:
        explicit WaypointsMapScript(uint32 id) : WorldMapScript(fmt::format("noble_next_waypoints_map_{}", id).c_str(), id) { }
        void OnUpdate(Map* map, uint32 diff) override { WaypointsService::Instance().UpdateMap(map, diff); }
    };
}

void AddSC_NobleNextWaypointsCommands()
{
    new RoleplayCore::NobleNext::WaypointsCommands();
    for (MapEntry const* entry : sMapStore)
        if (entry->IsWorldMap())
            new RoleplayCore::NobleNext::WaypointsMapScript(entry->ID);
}
