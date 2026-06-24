/*
 * NobleNext POI — GM commands (.poi).
 * Original POI system by ERINGAR.
 */

#include "custom_poi_handler.h"
#include "CharacterDatabase.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatCommandTags.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"

#include <cctype>
#include <vector>

#include <fmt/format.h>

namespace RoleplayCore
{
    using namespace Trinity::ChatCommands;

    static std::string NormalizeCommandText(std::string value)
    {
        for (char& ch : value)
            if (ch == '_')
                ch = ' ';

        while (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        return value;
    }

    static std::vector<std::string> TokenizePoiArgs(std::string_view input)
    {
        std::vector<std::string> tokens;
        std::string current;
        bool inQuote = false;
        bool escaped = false;

        for (char ch : input)
        {
            if (escaped)
            {
                current.push_back(ch);
                escaped = false;
                continue;
            }

            if (ch == '\\')
            {
                escaped = true;
                continue;
            }

            if (ch == '"')
            {
                inQuote = !inQuote;
                continue;
            }

            if (!inQuote && std::isspace(static_cast<unsigned char>(ch)))
            {
                if (!current.empty())
                {
                    tokens.push_back(NormalizeCommandText(current));
                    current.clear();
                }
                continue;
            }

            current.push_back(ch);
        }

        if (escaped)
            current.push_back('\\');

        if (!current.empty())
            tokens.push_back(NormalizeCommandText(current));

        return tokens;
    }

    static bool ParseUInt(std::string const& value, uint32& out)
    {
        try
        {
            size_t pos = 0;
            uint32 parsed = static_cast<uint32>(std::stoul(value, &pos));
            if (pos != value.size())
                return false;
            out = parsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    class PoiCommands : public CommandScript
    {
    public:
        PoiCommands() : CommandScript("noble_next_poi_commands") { }

        std::span<ChatCommandBuilder const> GetCommands() const override
        {
            static ChatCommandTable poiOwnerCommandTable =
            {
                { "",        HandlePOISetOwnerHelp,    LANG_COMMAND_POI_SET_OWNER_ORG_HELP,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "player",  HandlePOISetOwnerPlayer,  LANG_COMMAND_POI_SET_OWNER_PLAYER_HELP,  rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "org",     HandlePOISetOwnerOrg,     LANG_COMMAND_POI_SET_OWNER_ORG_HELP,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "system",  HandlePOISetOwnerSystem,  LANG_COMMAND_POI_SET_OWNER_SYSTEM_HELP,  rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "npc",     HandlePOISetOwnerNpc,     LANG_COMMAND_POI_SET_OWNER_NPC_HELP,     rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };

            static ChatCommandTable poiSetCommandTable =
            {
                { "",            HandlePOISetHelp,         LANG_COMMAND_POI_SET_NAME_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "name",        HandlePOISetName,        LANG_COMMAND_POI_SET_NAME_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "description", HandlePOISetDescription, LANG_COMMAND_POI_SET_DESCRIPTION_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "des",         HandlePOISetDescription, LANG_COMMAND_POI_SET_DESCRIPTION_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "position",    HandlePOISetPosition,    LANG_COMMAND_POI_SET_POSITION_HELP,    rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "type",        HandlePOISetType,        LANG_COMMAND_POI_SET_TYPE_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "icon",        HandlePOISetIcon,        LANG_COMMAND_POI_SET_TYPE_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "color",       HandlePOISetColor,       LANG_COMMAND_POI_SET_TYPE_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "apply",       HandlePOISetApply,       LANG_COMMAND_POI_SET_APPLY_HELP,       rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "owner",       poiOwnerCommandTable },
            };

            static ChatCommandTable poiUpdateCommandTable =
            {
                { "color",       HandlePOISetColor,       LANG_COMMAND_POI_SET_TYPE_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "icon",        HandlePOISetIcon,        LANG_COMMAND_POI_SET_TYPE_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
            };

            static ChatCommandTable poiCommandTable =
            {
                { "",       HandlePOIHelp,   LANG_COMMAND_POI_HELP,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "help",   HandlePOIHelp,   LANG_COMMAND_POI_HELP,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "create", HandlePOICreate, LANG_COMMAND_POI_CREATE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "delete", HandlePOIDelete, LANG_COMMAND_POI_DELETE_HELP, rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "sync",   HandlePOISync,   LANG_COMMAND_POI_SYNC_HELP,   rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "list",   HandlePOIList,   LANG_COMMAND_POI_HELP,        rbac::RBAC_PERM_COMMAND_GM_INGAME, Console::No },
                { "update", poiUpdateCommandTable },
                { "set",    poiSetCommandTable },
            };

            static ChatCommandTable commandTable =
            {
                { "poi", poiCommandTable },
            };

            return commandTable;
        }

        static bool HandlePOIHelp(ChatHandler* handler)
        {
            Trinity::ChatCommands::SendCommandHelpFor(*handler, "poi");
            return true;
        }

        static bool HandlePOISetHelp(ChatHandler* handler)
        {
            Trinity::ChatCommands::SendCommandHelpFor(*handler, "poi set");
            return true;
        }

        static bool HandlePOISetOwnerHelp(ChatHandler* handler)
        {
            Trinity::ChatCommands::SendCommandHelpFor(*handler, "poi set owner");
            return true;
        }

        static bool HandlePOIList(ChatHandler* handler)
        {
            if (!handler->GetPlayer())
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
                std::string owner = fields[6].GetString();

                handler->SendSysMessage(fmt::format(
                    "  #{} — {} (map {}, type {}, владелец: {})",
                    id, name, map, uint32(type), owner.empty() ? "?" : owner));
            } while (result->NextRow());

            return true;
        }

        static bool HandlePOICreate(ChatHandler* handler, Tail args)
        {
            if (!handler->GetPlayer())
                return false;

            std::vector<std::string> tokens = TokenizePoiArgs(args);
            std::string ownerToken = "self";
            std::string name;
            std::string description;
            uint32 legacyType = 0;
            uint32 color = 0;

            if (tokens.empty())
            {
                PoiHandler::Instance().Create(handler->GetPlayer());
                handler->SendSysMessage(LANG_COMMAND_POI_CREATED);
                return true;
            }

            // Short defaults:
            // .poi create Test
            // .poi create Test 2
            // .poi create Test 2 "Описание"
            // .poi create Test 2 "Описание" 1
            if (tokens.size() <= 4)
            {
                name = tokens[0];
                if (tokens.size() >= 2 && !ParseUInt(tokens[1], legacyType))
                {
                    handler->SendSysMessage("POI: второй аргумент в короткой форме должен быть legacyType 0-4.");
                    return false;
                }
                if (tokens.size() >= 3)
                    description = tokens[2];
                if (tokens.size() >= 4 && !ParseUInt(tokens[3], color))
                {
                    handler->SendSysMessage("POI: четвёртый аргумент в короткой форме должен быть color 0-6.");
                    return false;
                }
            }
            // Full sfpoi-compatible form:
            // .poi create <owner> <legacyType> <name> <description> <color>
            else if (tokens.size() == 5)
            {
                ownerToken = tokens[0];
                if (!ParseUInt(tokens[1], legacyType) || !ParseUInt(tokens[4], color))
                {
                    handler->SendSysMessage("POI: legacyType должен быть числом 0-4, color должен быть числом 0-6.");
                    return false;
                }
                name = tokens[2];
                description = tokens[3];
            }
            else
            {
                handler->SendSysMessage("Usage: .poi create [name] [legacyType 0-4] [description] [color 0-6]");
                handler->SendSysMessage("Full:  .poi create <owner> <legacyType 0-4> <name> <description> <color 0-6>");
                handler->SendSysMessage("Quotes supported: .poi create \"Горные бандиты\" 2 \"Дом у каналов\" \"Описание точки\" 1");
                handler->SendSysMessage("Escaped quotes supported: .poi create \"\\\"Дом у каналов\\\"\" 2 \"Дом\" \"Описание\" 1");
                handler->SendSysMessage("Underscores are converted to spaces: Дом_у_каналов.");
                return false;
            }

            if (legacyType > 4 || color > 6)
            {
                handler->SendSysMessage("POI: legacyType должен быть 0-4, color должен быть 0-6.");
                return false;
            }

            PoiHandler::Instance().CreateDetailed(handler->GetPlayer(), ownerToken, uint8(legacyType), name, description, uint8(color));
            handler->PSendSysMessage("POI создан: owner=%s name=%s legacyType=%u color=%u.", ownerToken.c_str(), name.c_str(), legacyType, color);
            return true;
        }

        static bool HandlePOIDelete(ChatHandler* handler, uint32 id)
        {
            PoiHandler::Instance().Delete(id);
            handler->PSendSysMessage(LANG_COMMAND_POI_DELETED, id);
            return true;
        }

        static bool HandlePOISync(ChatHandler* handler)
        {
            if (!handler->GetPlayer())
                return false;

            PoiHandler::Instance().SendFullList(handler->GetPlayer());
            handler->SendSysMessage(LANG_COMMAND_POI_SYNCED);
            return true;
        }

        static bool HandlePOISetName(ChatHandler* handler, uint32 id, Tail name)
        {
            PoiHandler::Instance().SetName(id, NormalizeCommandText(std::string(name)));
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetDescription(ChatHandler* handler, uint32 id, Tail description)
        {
            PoiHandler::Instance().SetDescription(id, NormalizeCommandText(std::string(description)));
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetPosition(ChatHandler* handler, uint32 id)
        {
            if (!handler->GetPlayer())
                return false;

            PoiHandler::Instance().SetPosition(id, handler->GetPlayer());
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetType(ChatHandler* handler, uint32 id, uint8 type)
        {
            if (type < 1 || type > 4)
            {
                handler->SendSysMessage(LANG_COMMAND_POI_INVALID_TYPE);
                return false;
            }

            PoiHandler::Instance().SetType(id, type);
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetIcon(ChatHandler* handler, uint32 id, std::string_view iconKey)
        {
            PoiHandler::Instance().SetIcon(id, std::string(iconKey));
            handler->PSendSysMessage("POI %u: icon=%s.", id, std::string(iconKey).c_str());
            return true;
        }

        static bool HandlePOISetColor(ChatHandler* handler, uint32 id, uint8 color)
        {
            if (color > 6)
            {
                handler->SendSysMessage("POI: color должен быть 0-6 (0=ничей, 1=Альянс, 2=Орда, 3=нейтрал, 4=общественное, 5=другое, 6=сюжет).");
                return false;
            }

            PoiHandler::Instance().SetColor(id, color);
            handler->PSendSysMessage("POI %u: color %u.", id, color);
            return true;
        }

        static bool HandlePOISetOwnerPlayer(ChatHandler* handler, uint32 id, Optional<std::string_view> targetName)
        {
            if (!handler->GetPlayer())
                return false;

            PoiHandler::Instance().SetOwnerPlayer(id, handler->GetPlayer(), targetName ? std::string(*targetName) : std::string());
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetOwnerOrg(ChatHandler* handler, uint32 id, Tail orgName)
        {
            PoiHandler::Instance().SetOwnerOrganization(id, NormalizeCommandText(std::string(orgName)));
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetOwnerSystem(ChatHandler* handler, uint32 id, Optional<std::string_view> label)
        {
            PoiHandler::Instance().SetOwnerSystem(id, label ? NormalizeCommandText(std::string(*label)) : std::string());
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetOwnerNpc(ChatHandler* handler, uint32 id, Tail npcName)
        {
            PoiHandler::Instance().SetOwnerNpc(id, NormalizeCommandText(std::string(npcName)));
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }

        static bool HandlePOISetApply(ChatHandler* handler, uint32 id, uint8 type, std::string_view iconKey, uint8 colorKey,
            uint8 ownerType, uint8 updatePosition, Tail ownerTail)
        {
            Player* player = handler->GetPlayer();
            if (!player)
                return false;

            if (type < 1 || type > 4)
            {
                handler->SendSysMessage(LANG_COMMAND_POI_INVALID_TYPE);
                return false;
            }

            if (colorKey > 6)
            {
                handler->SendSysMessage("POI: color должен быть 0-6.");
                return false;
            }

            if (ownerType < POI_OWNER_PLAYER || ownerType > POI_OWNER_NPC)
            {
                handler->SendSysMessage("POI: ownerType должен быть 1-4 (player/org/system/npc).");
                return false;
            }

            std::string ownerText = NormalizeCommandText(std::string(ownerTail));

            PoiHandler::Instance().ApplyProperties(id, player, type, std::string(iconKey), colorKey, ownerType, ownerText, updatePosition != 0);
            handler->PSendSysMessage(LANG_COMMAND_POI_UPDATED, id);
            return true;
        }
    };
}

void AddSC_CustomPoiCommands()
{
    new RoleplayCore::PoiCommands();
}
