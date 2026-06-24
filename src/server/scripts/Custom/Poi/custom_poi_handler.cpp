/*
 * NobleNext POI — server handler.
 * Original POI system by ERINGAR.
 */

#include "custom_poi_handler.h"

#include "CharacterCache.h"
#include "CharacterDatabase.h"
#include "ChatPackets.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

#include <fmt/format.h>

namespace RoleplayCore
{
    static constexpr char POI_ADDON_PREFIX[] = "RPC_POI_INFO";
    static std::string NormalizeTextToken(std::string value)
    {
        for (char& ch : value)
            if (ch == '_')
                ch = ' ';

        while (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);

        return value;
    }

    static uint8 NormalizeLegacyPoiType(uint8 legacyType)
    {
        // sfpoi: 0=Other, 1=Camp, 2=House, 3=Tower, 4=Story.
        // NobleNext: 1=Info/Misc, 2=Story, 3=Camp/House, 4=Tower/Danger.
        switch (legacyType)
        {
            case 4: return 2;
            case 1:
            case 2: return 3;
            case 3: return 4;
            default: return 1;
        }
    }

    static std::string IconForLegacyType(uint8 legacyType)
    {
        switch (legacyType)
        {
            case 4: return "Story";
            case 1: return "Camp";
            case 2: return "House";
            case 3: return "Tower";
            default: return "Misc";
        }
    }

    static void ExecuteSavePoi(PoiData const& poi)
    {
        std::string escName = poi.Name;
        std::string escDesc = poi.Description;
        std::string escOwnerName = poi.OwnerName;
        std::string escIconKey = poi.IconKey;
        CharacterDatabase.EscapeString(escName);
        CharacterDatabase.EscapeString(escDesc);
        CharacterDatabase.EscapeString(escOwnerName);
        CharacterDatabase.EscapeString(escIconKey);

        std::string sql = fmt::format(
            "REPLACE INTO `ng_character_poi` (`id`, `owner_guid`, `owner_type`, `owner_name`, `type`, `x`, `y`, `map`, `name`, `description`, `icon_key`, `color_key`) "
            "VALUES ({}, {}, {}, '{}', {}, {}, {}, {}, '{}', '{}', '{}', {})",
            poi.ID, poi.OwnerGuid, poi.OwnerType, escOwnerName, poi.Type, poi.X, poi.Y, poi.Map, escName, escDesc, escIconKey, poi.ColorKey);
        CharacterDatabase.Execute(sql.c_str());
    }

    static std::string DefaultIconForType(uint8 type)
    {
        switch (type)
        {
            case 2: return "Story";
            case 3: return "Camp";
            case 4: return "Tower";
            default: return "Misc";
        }
    }

    std::string PoiData::GetDataPacket() const
    {
        std::string icon = IconKey.empty() ? DefaultIconForType(Type) : IconKey;
        return fmt::format("{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}[&]{}",
            ID,
            Owner,
            Name,
            Description,
            Type,
            X,
            Y,
            Map,
            Deleted ? "true" : "false",
            OwnerType,
            OwnerGuid,
            icon,
            ColorKey);
    }

    void PoiData::RefreshOwnerDisplay()
    {
        if (OwnerType == POI_OWNER_ORGANIZATION || OwnerType == POI_OWNER_SYSTEM || OwnerType == POI_OWNER_NPC)
        {
            if (OwnerType == POI_OWNER_SYSTEM)
                Owner = "System";
            else
                Owner = OwnerName.empty() ? (OwnerType == POI_OWNER_NPC ? "NPC" : "Организация") : OwnerName;
            return;
        }

        if (!OwnerName.empty())
        {
            Owner = OwnerName;
            return;
        }

        if (OwnerGuid)
        {
            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(OwnerGuid);
            if (Player* owner = ObjectAccessor::FindPlayerByLowGUID(guid.GetCounter()))
                Owner = owner->GetName();
            else if (CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(guid))
                Owner = cache->Name;
            else
                Owner = "Unknown";
            return;
        }

        Owner = "Unknown";
    }

    PoiHandler& PoiHandler::Instance()
    {
        static PoiHandler instance;
        return instance;
    }

    void PoiHandler::SendAddonMessage(Player* player, std::string const& message)
    {
        if (!player)
            return;

        WorldPackets::Chat::Chat packet;
        packet.Initialize(CHAT_MSG_WHISPER, LANG_ADDON, player, player, message, 0, "", DEFAULT_LOCALE, POI_ADDON_PREFIX);
        player->SendDirectMessage(packet.Write());
    }

    void PoiHandler::Initialize()
    {
        _poiMap.clear();

        QueryResult result = CharacterDatabase.Query(
            "SELECT `id`, `owner_guid`, `type`, `x`, `y`, `map`, `name`, `description`, "
            "COALESCE(`owner_type`, 1), COALESCE(`owner_name`, ''), COALESCE(`icon_key`, ''), COALESCE(`color_key`, 0) "
            "FROM `ng_character_poi` ORDER BY `id`");

        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            auto data = std::make_unique<PoiData>();
            data->ID = fields[0].GetUInt32();
            data->OwnerGuid = fields[1].GetUInt64();
            data->Type = fields[2].GetUInt8();
            data->X = fields[3].GetFloat();
            data->Y = fields[4].GetFloat();
            data->Map = fields[5].GetUInt32();
            data->Name = fields[6].GetString();
            data->Description = fields[7].GetString();
            data->OwnerType = fields[8].GetUInt8();
            data->OwnerName = fields[9].GetString();
            data->IconKey = fields[10].GetString();
            data->ColorKey = fields[11].GetUInt8();
            data->Deleted = false;
            data->RefreshOwnerDisplay();

            _poiMap.emplace(data->ID, std::move(data));
        } while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} NobleNext POI(s)", _poiMap.size());
    }

    PoiData* PoiHandler::GetPoi(uint32 id)
    {
        auto itr = _poiMap.find(id);
        return itr != _poiMap.end() ? itr->second.get() : nullptr;
    }

    void PoiHandler::Create(Player* initiator)
    {
        if (!initiator)
            return;

        uint32 nextId = 1;
        if (!_poiMap.empty())
            nextId = _poiMap.rbegin()->first + 1;

        auto data = std::make_unique<PoiData>();
        data->ID = nextId;
        data->OwnerType = POI_OWNER_PLAYER;
        data->OwnerGuid = initiator->GetGUID().GetCounter();
        data->OwnerName = initiator->GetName();
        data->IconKey = DefaultIconForType(1);
        data->Type = 1;
        data->Name = fmt::format("Точка интереса #{}", nextId);
        data->Description.clear();
        data->X = initiator->GetPositionX();
        data->Y = initiator->GetPositionY();
        data->Map = initiator->GetMapId();
        data->RefreshOwnerDisplay();

        uint32 newId = nextId;
        _poiMap.emplace(newId, std::move(data));

        if (PoiData* saved = GetPoi(newId))
        {
            ExecuteSavePoi(*saved);
            BroadcastUpdate(newId);
        }
    }

    void PoiHandler::CreateDetailed(Player* initiator, std::string const& ownerToken, uint8 legacyType, std::string const& name,
        std::string const& description, uint8 colorKey)
    {
        if (!initiator)
            return;

        uint32 nextId = 1;
        if (!_poiMap.empty())
            nextId = _poiMap.rbegin()->first + 1;

        auto data = std::make_unique<PoiData>();
        data->ID = nextId;
        data->Type = NormalizeLegacyPoiType(legacyType);
        data->IconKey = IconForLegacyType(legacyType);
        data->ColorKey = colorKey <= 6 ? colorKey : 0;
        data->Name = NormalizeTextToken(name);
        data->Description = NormalizeTextToken(description);
        data->X = initiator->GetPositionX();
        data->Y = initiator->GetPositionY();
        data->Map = initiator->GetMapId();

        std::string owner = NormalizeTextToken(ownerToken);
        if (owner.empty() || owner == "me" || owner == "self")
        {
            data->OwnerType = POI_OWNER_PLAYER;
            data->OwnerGuid = initiator->GetGUID().GetCounter();
            data->OwnerName = initiator->GetName();
        }
        else if (owner == "system" || owner == "server")
        {
            data->OwnerType = POI_OWNER_SYSTEM;
            data->OwnerGuid = 0;
            data->OwnerName = "System";
        }
        else if (owner.rfind("npc:", 0) == 0)
        {
            data->OwnerType = POI_OWNER_NPC;
            data->OwnerGuid = 0;
            data->OwnerName = owner.substr(4);
        }
        else
        {
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(owner);
            if (!guid.IsEmpty())
            {
                data->OwnerType = POI_OWNER_PLAYER;
                data->OwnerGuid = guid.GetCounter();
                data->OwnerName = owner;
            }
            else
            {
                data->OwnerType = POI_OWNER_ORGANIZATION;
                data->OwnerGuid = 0;
                data->OwnerName = owner;
            }
        }
        data->RefreshOwnerDisplay();

        uint32 newId = nextId;
        _poiMap.emplace(newId, std::move(data));

        if (PoiData* saved = GetPoi(newId))
        {
            ExecuteSavePoi(*saved);
            BroadcastUpdate(newId);
        }
    }

    void PoiHandler::Delete(uint32 id)
    {
        PoiData* poi = GetPoi(id);
        if (!poi)
            return;

        poi->Deleted = true;
        BroadcastUpdate(id);
        std::string sql = fmt::format("DELETE FROM `ng_character_poi` WHERE `id` = {}", id);
        CharacterDatabase.Execute(sql.c_str());
        _poiMap.erase(id);
    }

    void PoiHandler::SetName(uint32 id, std::string const& name)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->Name = name;
            SavePoi(id);
        }
    }

    void PoiHandler::SetDescription(uint32 id, std::string const& description)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->Description = description;
            SavePoi(id);
        }
    }

    void PoiHandler::SetPosition(uint32 id, Player* initiator)
    {
        if (!initiator)
            return;

        if (PoiData* poi = GetPoi(id))
        {
            poi->X = initiator->GetPositionX();
            poi->Y = initiator->GetPositionY();
            poi->Map = initiator->GetMapId();
            SavePoi(id);
        }
    }

    void PoiHandler::SetType(uint32 id, uint8 type)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->Type = type;
            poi->IconKey = DefaultIconForType(type);
            SavePoi(id);
        }
    }

    void PoiHandler::SetIcon(uint32 id, std::string const& iconKey)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->IconKey = iconKey.empty() ? DefaultIconForType(poi->Type) : iconKey;
            SavePoi(id);
        }
    }

    void PoiHandler::SetColor(uint32 id, uint8 colorKey)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->ColorKey = colorKey <= 6 ? colorKey : 0;
            SavePoi(id);
        }
    }

    void PoiHandler::SetOwnerPlayer(uint32 id, Player* initiator, std::string const& targetName)
    {
        if (!initiator)
            return;

        if (PoiData* poi = GetPoi(id))
        {
            poi->OwnerType = POI_OWNER_PLAYER;
            if (targetName.empty())
            {
                poi->OwnerGuid = initiator->GetGUID().GetCounter();
                poi->OwnerName = initiator->GetName();
            }
            else
            {
                ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(targetName);
                poi->OwnerGuid = guid.IsEmpty() ? 0 : guid.GetCounter();
                poi->OwnerName = targetName;
            }
            poi->RefreshOwnerDisplay();
            SavePoi(id);
        }
    }

    void PoiHandler::SetOwnerOrganization(uint32 id, std::string const& orgName)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->OwnerType = POI_OWNER_ORGANIZATION;
            poi->OwnerGuid = 0;
            poi->OwnerName = orgName;
            poi->RefreshOwnerDisplay();
            SavePoi(id);
        }
    }

    void PoiHandler::SetOwnerSystem(uint32 id, std::string const& /*label*/)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->OwnerType = POI_OWNER_SYSTEM;
            poi->OwnerGuid = 0;
            poi->OwnerName = "System";
            poi->RefreshOwnerDisplay();
            SavePoi(id);
        }
    }

    void PoiHandler::SetOwnerNpc(uint32 id, std::string const& npcName)
    {
        if (PoiData* poi = GetPoi(id))
        {
            poi->OwnerType = POI_OWNER_NPC;
            poi->OwnerGuid = 0;
            poi->OwnerName = npcName;
            poi->RefreshOwnerDisplay();
            SavePoi(id);
        }
    }

    void PoiHandler::ApplyProperties(uint32 id, Player* initiator, uint8 type, std::string const& iconKey, uint8 colorKey,
        uint8 ownerType, std::string const& ownerText, bool updatePosition)
    {
        if (!initiator)
            return;

        PoiData* poi = GetPoi(id);
        if (!poi)
            return;

        if (type >= 1 && type <= 4)
            poi->Type = type;

        poi->IconKey = iconKey.empty() ? DefaultIconForType(poi->Type) : iconKey;
        poi->ColorKey = colorKey <= 6 ? colorKey : 0;

        if (updatePosition)
        {
            poi->X = initiator->GetPositionX();
            poi->Y = initiator->GetPositionY();
            poi->Map = initiator->GetMapId();
        }

        switch (ownerType)
        {
            case POI_OWNER_ORGANIZATION:
                poi->OwnerType = POI_OWNER_ORGANIZATION;
                poi->OwnerGuid = 0;
                poi->OwnerName = ownerText;
                break;
            case POI_OWNER_SYSTEM:
                poi->OwnerType = POI_OWNER_SYSTEM;
                poi->OwnerGuid = 0;
                poi->OwnerName = "System";
                break;
            case POI_OWNER_NPC:
                poi->OwnerType = POI_OWNER_NPC;
                poi->OwnerGuid = 0;
                poi->OwnerName = ownerText;
                break;
            case POI_OWNER_PLAYER:
            default:
                poi->OwnerType = POI_OWNER_PLAYER;
                if (ownerText.empty())
                {
                    poi->OwnerGuid = initiator->GetGUID().GetCounter();
                    poi->OwnerName = initiator->GetName();
                }
                else
                {
                    ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(ownerText);
                    poi->OwnerGuid = guid.IsEmpty() ? 0 : guid.GetCounter();
                    poi->OwnerName = ownerText;
                }
                break;
        }

        poi->RefreshOwnerDisplay();
        SavePoi(id);
    }

    void PoiHandler::SavePoi(uint32 id)
    {
        PoiData* poi = GetPoi(id);
        if (!poi || poi->Deleted)
            return;

        ExecuteSavePoi(*poi);
        BroadcastUpdate(id);
    }

    void PoiHandler::SendUpdate(Player* player, PoiData const& data) const
    {
        SendAddonMessage(player, data.GetDataPacket());
    }

    void PoiHandler::BroadcastUpdate(uint32 id) const
    {
        auto itr = _poiMap.find(id);
        if (itr == _poiMap.end())
            return;

        PoiData const& data = *itr->second;
        std::string packet = data.GetDataPacket();

        uint32 recipients = 0;
        SessionMap const& sessions = sWorld->GetAllSessions();
        for (auto const& [_, session] : sessions)
        {
            Player* player = session ? session->GetPlayer() : nullptr;
            if (!player || !player->IsInWorld())
                continue;

            ObjectGuid playerGuid = player->GetGUID();
            player->m_Events.AddEventAtOffset([playerGuid, packet]()
            {
                if (Player* online = ObjectAccessor::FindPlayer(playerGuid))
                    if (online->IsInWorld())
                        SendAddonMessage(online, packet);
            }, Milliseconds(0));
            ++recipients;
        }

        TC_LOG_INFO("server.loading", "NobleNext POI delta: id={} deleted={} recipients={} payloadSize={}",
            data.ID, data.Deleted, recipients, packet.size());
    }

    void PoiHandler::SendFullList(Player* player, uint32 initialDelayMs) const
    {
        if (!player)
            return;

        ObjectGuid playerGuid = player->GetGUID();
        uint32 delayMs = initialDelayMs;
        static constexpr uint32 POI_PACKET_DELAY_MS = 10;

        for (auto const& [id, data] : _poiMap)
        {
            if (!data)
                continue;

            std::string packet = data->GetDataPacket();
            player->m_Events.AddEventAtOffset([playerGuid, packet = std::move(packet)]()
            {
                if (Player* online = ObjectAccessor::FindPlayer(playerGuid))
                    if (online->IsInWorld())
                        SendAddonMessage(online, packet);
            }, Milliseconds(delayMs));

            delayMs += POI_PACKET_DELAY_MS;
        }
    }
}
