/*
 * NobleNext Battle — aura stack helpers.
 */

#pragma once

#include "noble_next_battle_constants.h"

class ChatHandler;
class Player;
class Unit;

namespace RoleplayCore::NobleNext::Battle
{
    bool SetAuraStack(Unit* target, uint32 spellId, int32 value, ChatHandler* handler, char const* label);
    bool AddAuraStack(Unit* target, uint32 spellId, int32 delta, ChatHandler* handler, char const* label);
    bool RemoveAuraStack(Unit* target, uint32 spellId, int32 delta, ChatHandler* handler, char const* label);
    bool SetIndexedAura(Unit* target, AuraEntry const* entry, int32 value, ChatHandler* handler);
    bool RemoveIndexedAura(Unit* target, AuraEntry const* entry, ChatHandler* handler);
    void WakeupTarget(Unit* target, Player* issuer, ChatHandler* handler);
}
