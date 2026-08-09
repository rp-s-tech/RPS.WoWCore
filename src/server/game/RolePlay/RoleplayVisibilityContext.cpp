#include "RoleplayVisibilityContext.h"

#include "Corpse.h"
#include "Creature.h"
#include "GameObject.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RoleplayPhaseMgr.h"

namespace RoleplayVisibilityContext
{
uint64 Resolve(WorldObject const& object)
{
    if (Player const* player = object.ToPlayer())
        return sRoleplayPhaseMgr.GetPlayerPhaseId(player->GetGUID().GetCounter(), player->GetMapId());

    if (Creature const* creature = object.ToCreature(); creature && creature->GetSpawnId())
        return sRoleplayPhaseMgr.GetSpawnPhaseId(RoleplayPhaseSpawnType::Creature, creature->GetSpawnId(), object.GetMapId());

    if (GameObject const* gameObject = object.ToGameObject(); gameObject && gameObject->GetSpawnId())
        return sRoleplayPhaseMgr.GetSpawnPhaseId(RoleplayPhaseSpawnType::GameObject, gameObject->GetSpawnId(), object.GetMapId());

    // Transient owner-derived objects (pets, summons, traps and corpses) do
    // not have persistent rp_phase_spawn rows. Resolve them through the
    // owner at visibility time so a context transition cannot leak them.
    if (Corpse const* corpse = object.ToCorpse())
        if (Player* owner = ObjectAccessor::FindConnectedPlayer(corpse->GetOwnerGUID()))
            return sRoleplayPhaseMgr.GetPlayerPhaseId(owner->GetGUID().GetCounter(), object.GetMapId());

    if (Player* owner = object.GetCharmerOrOwnerPlayerOrPlayerItself())
        return sRoleplayPhaseMgr.GetPlayerPhaseId(owner->GetGUID().GetCounter(), object.GetMapId());

    return 0;
}

bool CanShare(WorldObject const& left, WorldObject const& right)
{
    if (&left == &right)
        return true;

    return RoleplayPhaseMgr::SharesExclusiveContext(Resolve(left), Resolve(right));
}

bool CanSeeInPhaseContexts(WorldObject const& viewer, WorldObject const& target, bool ignoreNative /*= false*/)
{
    if (!CanShare(viewer, target))
        return false;

    if (ignoreNative)
        return true;

    return viewer.GetPhaseShift().CanSee(target.GetPhaseShift());
}
}
