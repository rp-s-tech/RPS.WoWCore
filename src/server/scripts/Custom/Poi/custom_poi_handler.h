#pragma once

/*
 * NobleNext POI — server handler.
 * Original POI system by ERINGAR.
 */

#include "Define.h"
#include <map>
#include <memory>
#include <string>

class Player;

namespace RoleplayCore
{
    enum PoiOwnerType : uint8
    {
        POI_OWNER_PLAYER       = 1,
        POI_OWNER_ORGANIZATION = 2,
        POI_OWNER_SYSTEM       = 3,
        POI_OWNER_NPC          = 4,
    };

    struct PoiData
    {
        uint32 ID = 0;
        std::string Owner;
        uint8 OwnerType = POI_OWNER_PLAYER;
        uint64 OwnerGuid = 0;
        std::string OwnerName;
        std::string IconKey;
        uint8 ColorKey = 0;
        uint8 Type = 0;
        float X = 0.f;
        float Y = 0.f;
        uint32 Map = 0;
        std::string Name;
        std::string Description;
        bool Deleted = false;

        std::string GetDataPacket() const;
        void RefreshOwnerDisplay();
    };

    class PoiHandler
    {
    public:
        static PoiHandler& Instance();

        void Initialize();
        void Create(Player* initiator);
        void CreateDetailed(Player* initiator, std::string const& ownerToken, uint8 legacyType, std::string const& name,
            std::string const& description, uint8 colorKey);
        void Delete(uint32 id);
        void SetName(uint32 id, std::string const& name);
        void SetDescription(uint32 id, std::string const& description);
        void SetPosition(uint32 id, Player* initiator);
        void SetType(uint32 id, uint8 type);
        void SetIcon(uint32 id, std::string const& iconKey);
        void SetColor(uint32 id, uint8 colorKey);
        void SetOwnerPlayer(uint32 id, Player* initiator, std::string const& targetName);
        void SetOwnerOrganization(uint32 id, std::string const& orgName);
        void SetOwnerSystem(uint32 id, std::string const& label);
        void SetOwnerNpc(uint32 id, std::string const& npcName);
        void ApplyProperties(uint32 id, Player* initiator, uint8 type, std::string const& iconKey, uint8 colorKey,
            uint8 ownerType, std::string const& ownerText, bool updatePosition);
        void SendFullList(Player* player, uint32 initialDelayMs = 0) const;

    private:
        PoiHandler() = default;

        PoiData* GetPoi(uint32 id);
        void SavePoi(uint32 id);
        void BroadcastUpdate(uint32 id) const;
        void SendUpdate(Player* player, PoiData const& data) const;
        static void SendAddonMessage(Player* player, std::string const& message);

        std::map<uint32, std::unique_ptr<PoiData>> _poiMap;
    };
}
