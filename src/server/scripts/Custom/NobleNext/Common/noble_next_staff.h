/*
 * NobleNext — staff / DM permission helpers.
 */

#pragma once

class Group;
class Player;
class Unit;

namespace RoleplayCore::NobleNext
{
    constexpr uint32 CREATIVE_DM_PHASE = 1024;

    bool HasStaffPermission(Player const* player);
    bool IsCreativeDm(Player const* player);
    bool CanControlBattleTarget(Player const* player, Unit* target);
    bool CanControlOwnedCreature(Player const* player, Unit* creature);
}
