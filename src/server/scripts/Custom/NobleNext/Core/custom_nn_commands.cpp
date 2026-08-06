/*
 * NobleNext — .nnstatus, .nn help, .poilist alias.
 */

#include "noble_next_staff.h"

#include "CharacterDatabase.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Player.h"
#include "RBAC.h"

#include <fmt/format.h>

namespace RoleplayCore::NobleNext
{
    using namespace Trinity::ChatCommands;

    class CoreCommands : public CommandScript
    {
    public:
        CoreCommands() : CommandScript("noble_next_core_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable nnCommandTable =
            {
                { "help",   HandleNnHelp,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "status", HandleNnStatus, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };

            static ChatCommandTable commandTable =
            {
                { "nn", nnCommandTable },
                { "nnstatus", HandleNnStatus, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "poilist",  HandlePoiList,  rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };
            return commandTable;
        }

        static bool HandleNnStatus(ChatHandler* handler)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;

            handler->SendSysMessage("[NobleNext] Статус модулей (C++ + Lua)");
            handler->SendSysMessage("  POI — Custom/Poi (C++)");
            handler->SendSysMessage("  Weather, Daytime, GobTele, GobMover — Custom/NobleNext (C++)");
            handler->SendSysMessage("  Waypoints, Pet, Army, Master, Battle — Custom/NobleNext (C++)");
            handler->SendSysMessage("  AIO UI — NobleNextLua (Eluna)");
            handler->SendSysMessage("Документация: Docs/NOBLENEXT.md");
            return true;
        }

        static bool HandleNnHelp(ChatHandler* handler)
        {
            if (!handler->GetPlayer() || !HasStaffPermission(handler->GetPlayer()))
                return false;

            handler->SendSysMessage("NobleNext GM-команды (C++):");
            handler->SendSysMessage("  .poi*, .poilist, .weather, .daytime, .setgrouptime");
            handler->SendSysMessage("  .gob tele <guid>, .movego, .wpmove/.wpgo, .pet*, .npccontrol");
            handler->SendSysMessage("  .nnsay/.nnyell, .sethp/.setbuff/.npcsetstat, .wakeup");
            handler->SendSysMessage("Vanilla / TC:");
            handler->SendSysMessage("  .gob add/.gob delete/.gob near, .npc delete, .summon, .modify phase/scale");
            return true;
        }

        static bool HandlePoiList(ChatHandler* handler)
        {
            Player* player = handler->GetPlayer();
            if (!player || !HasStaffPermission(player))
                return false;

            QueryResult result = CharacterDatabase.Query(
                "SELECT id, name, type, map, description, "
                "COALESCE(owner_type, 1), COALESCE(owner_name, ''), COALESCE(color_key, 0) "
                "FROM ng_character_poi ORDER BY id");

            if (!result)
            {
                handler->SendSysMessage("[POI] Нет точек.");
                return true;
            }

            handler->SendSysMessage("[POI] Список:");
            do
            {
                Field* fields = result->Fetch();
                uint32 id = fields[0].GetUInt32();
                std::string name = fields[1].GetString();
                uint8 type = fields[2].GetUInt8();
                uint32 map = fields[3].GetUInt32();
                std::string desc = fields[4].GetString();
                std::string owner = fields[6].GetString();

                handler->SendSysMessage(fmt::format(
                    "  #{} — {} (map {}, type {}, владелец: {})",
                    id, name, map, uint32(type), owner.empty() ? "?" : owner));
            } while (result->NextRow());

            return true;
        }
    };
}

void AddSC_NobleNextCoreCommands()
{
    new RoleplayCore::NobleNext::CoreCommands();
}
