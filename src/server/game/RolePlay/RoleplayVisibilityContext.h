#pragma once

#include "Define.h"

class WorldObject;

// Read-only logical RP visibility policy. Native PhaseShift is never mutated here.
// WorldObject::InSamePhase remains the upstream PhaseShift::CanSee predicate;
// spatial/interaction code that must honour both layers uses CanSeeInPhaseContexts.
namespace RoleplayVisibilityContext
{
    // Map-aware logical context for a world object. Persistent creature/GO use
    // rp_phase_spawn; players use active character context; owner-derived
    // transient objects (pets, summons, traps, corpses) inherit the owner.
    // Objects without a mapping resolve to common (0).
    TC_GAME_API uint64 Resolve(WorldObject const& object);

    // Exclusive v1: same logical id only (0/0, A/A).
    TC_GAME_API bool CanShare(WorldObject const& left, WorldObject const& right);

    // Composite gate: native PhaseShift::CanSee AND logical exclusive match.
    // ignoreNative=true bypasses only PhaseShift (SPELL_ATTR6_IGNORE_PHASE_SHIFT);
    // logical isolation always remains enforced.
    TC_GAME_API bool CanSeeInPhaseContexts(WorldObject const& viewer, WorldObject const& target,
        bool ignoreNative = false);
}
