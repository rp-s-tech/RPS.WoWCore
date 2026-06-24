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
        static GobTeleHandler& Instance();

        void LoadGossipEntries();
        bool TryTeleport(Player* player, uint32 goGuidLow);
        bool SetTeleportDestination(Player* player, uint32 goGuidLow);
        void BindEntryScript(uint32 entry);

    private:
        std::unordered_set<uint32> _gossipEntries;
        void RegisterGossipForEntry(uint32 entry);
    };
}
