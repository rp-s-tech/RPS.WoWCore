/*
 * NobleNext — NN_GOBGROUP addon protocol (self-whisper, LANG_ADDON).
 */

#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "Optional.h"

#include <string>
#include <string_view>
#include <vector>

class Player;

namespace RoleplayCore::NobleNext
{
    struct GobGroupInfoSnapshot;
    struct GobGroupListSnapshot;
    struct GobGroupNearSnapshot;
    struct GobGroupStatusSnapshot;
    struct GobBlueprintListItem;
    struct GobBlueprintRecord;

    namespace GobGroupProtocol
    {
        static constexpr char ADDON_PREFIX[] = "NN_GOBGROUP";
        // ChatHandler text limit is 255; keep body under that with margin for type tags.
        static constexpr size_t MAX_PAYLOAD = 240;

        std::string EscapeField(std::string_view value);
        void SendRaw(Player* player, std::string const& body);

        void SendResult(Player* player, std::string_view verb, std::string_view status, std::string_view detail = {});
        void SendCapabilities(Player* player);
        void SendInfo(Player* player, GobGroupInfoSnapshot const& snap);
        void SendList(Player* player, GobGroupListSnapshot const& snap);
        void SendNear(Player* player, GobGroupNearSnapshot const& snap);
        void SendStatus(Player* player, GobGroupStatusSnapshot const& snap);

        // GobBlueprint (same NN_GOBGROUP prefix)
        void SendBlueprintList(Player* player, std::vector<GobBlueprintListItem> const& items, std::string_view filter);
        void SendBlueprintInfo(Player* player, GobBlueprintRecord const& record);
        void SendBlueprintResult(Player* player, std::string_view verb, std::string_view status, std::string_view detail = {});
        void SendBlueprintStatus(Player* player, std::string_view statusLine);
    }
}
