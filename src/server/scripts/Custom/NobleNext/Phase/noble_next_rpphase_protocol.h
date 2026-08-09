/*
 * NobleNext — NN_RPPHASE addon protocol (self-whisper, LANG_ADDON).
 *
 * RP phase IDs are always serialized as decimal strings. Lua must never
 * coerce them to a number because phase_id is BIGINT UNSIGNED.
 */

#pragma once

#include "Define.h"

#include <string>
#include <string_view>

class Player;

namespace RoleplayCore::NobleNext::RoleplayPhaseProtocol
{
    static constexpr char ADDON_PREFIX[] = "NN_RPPHASE";
    static constexpr size_t MAX_PAYLOAD = 220;

    std::string EscapeField(std::string_view value);
    void SendRaw(Player* player, std::string const& body);

    // Registers the protected client->server route and the TransitionPlayer
    // notification hook. Invoke once from the NobleNext script loader.
    void RegisterHandlers();

    // Returns false when CONTEXT multipart could not be fully delivered.
    bool SendContext(Player* player, std::string_view requestId = "0");
    void SendTransitionEvent(Player* player, uint64 previousPhaseId, uint64 currentPhaseId);
}
