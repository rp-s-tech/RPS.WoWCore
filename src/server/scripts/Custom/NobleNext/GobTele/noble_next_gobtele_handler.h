/*
 * NobleNext — gameobject teleport handler.
 */

#pragma once

#include "Define.h"
#include <unordered_set>

class Player;

namespace RoleplayCore::NobleNext
{
    class GobTeleHandler
    {
    public:
        static constexpr uint32 HousingMapId = 2783;

        static GobTeleHandler& Instance();

        void LoadGossipEntries();
        bool TryTeleport(Player* player, uint32 goGuidLow);
        bool SetTeleportDestination(Player* player, uint32 goGuidLow);
        bool BindEntryScript(uint32 entry);
        void Update();

    private:
        std::unordered_set<uint32> _gossipEntries;
        std::unordered_set<uint32> _pending;
        void RegisterGossipForEntry(uint32 entry);
    };
}
