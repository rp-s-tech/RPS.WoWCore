/*
 * NobleNext — staff / DM permission helpers.
 */

#include "noble_next_staff.h"

#include "Group.h"
#include "PhasingHandler.h"
#include "Player.h"
#include "Unit.h"

namespace RoleplayCore::NobleNext
{
    bool HasStaffPermission(Player const* player)
    {
        if (!player)
            return false;
        if (player->GetSession()->GetSecurity() > SEC_PLAYER)
            return true;
        return IsCreativeDm(player);
    }

    bool IsCreativeDm(Player const* player)
    {
        if (!player)
            return false;
        return player->GetSession()->GetSecurity() >= SEC_MODERATOR
            && player->GetPhaseShift().HasPhase(CREATIVE_DM_PHASE);
    }

    bool CanControlOwnedCreature(Player const* player, Unit* creature)
    {
        if (!player || !creature)
            return false;
        if (player->GetSession()->GetSecurity() > SEC_PLAYER)
            return true;
        if (!IsCreativeDm(player))
            return false;
        return creature->GetOwnerGUID() == player->GetGUID();
    }

    bool CanControlBattleTarget(Player const* player, Unit* target)
    {
        if (!player || !target)
            return false;

        if (player->GetSession()->GetSecurity() > SEC_PLAYER)
            return true;

        if (!IsCreativeDm(player))
            return false;

        if (target->IsCreature() && target->GetOwnerGUID() == player->GetGUID())
            return true;

        if (Player* targetPlayer = target->ToPlayer())
        {
            if (Group const* group = player->GetGroup())
            {
                if (group->IsLeader(player->GetGUID()) && group->IsMember(targetPlayer->GetGUID()))
                    return true;
            }
        }

        return false;
    }
}
