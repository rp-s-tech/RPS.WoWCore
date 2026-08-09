/*
 * Native PhaseShift + logical RP visibility regression tests.
 */

#include "tc_catch2.h"

#include "DBCEnums.h"
#include "PhaseShift.h"
#include "PhasingHandler.h"
#include "RoleplayPhaseMgr.h"
#include "TerrainPreset.h"

namespace
{
PhaseShift MakePhased(uint32 phaseId, PhaseFlags flags = PhaseFlags::None)
{
    PhaseShift shift;
    shift.ClearPhases();
    shift.AddPhase(phaseId, flags, nullptr);
    return shift;
}
}

TEST_CASE("PhaseShift::CanSee default unphased pair", "[PhaseVisibility][native]")
{
    PhaseShift left;
    PhaseShift right;
    REQUIRE(left.CanSee(right));
    REQUIRE(right.CanSee(left));
}

TEST_CASE("PhaseShift::CanSee same and different non-cosmetic phases", "[PhaseVisibility][native]")
{
    PhaseShift sameA = MakePhased(170);
    PhaseShift sameB = MakePhased(170);
    PhaseShift other = MakePhased(171);

    REQUIRE(sameA.CanSee(sameB));
    REQUIRE_FALSE(sameA.CanSee(other));
}

TEST_CASE("PhaseShift::CanSee AlwaysVisible bypass", "[PhaseVisibility][native]")
{
    PhaseShift phased = MakePhased(170);
    PhaseShift const& always = PhasingHandler::GetAlwaysVisiblePhaseShift();

    REQUIRE(always.CanSee(phased));
    REQUIRE(phased.CanSee(always));
}

TEST_CASE("PhaseShift::CanSee cosmetic phases do not grant visibility alone", "[PhaseVisibility][native]")
{
    // Non-cosmetic phases clear Unphased; a shared cosmetic phase alone must not
    // reconnect two objects that otherwise have no common phase.
    PhaseShift left = MakePhased(170);
    left.AddPhase(200, PhaseFlags::Cosmetic, nullptr);
    PhaseShift right = MakePhased(171);
    right.AddPhase(200, PhaseFlags::Cosmetic, nullptr);

    REQUIRE_FALSE(left.CanSee(right));
}

TEST_CASE("PhaseShift::CanSee inverse vs disjoint normal phase", "[PhaseVisibility][native]")
{
    PhaseShift normal = MakePhased(170);
    PhaseShift inverse;
    PhasingHandler::InitDbPhaseShift(inverse, PHASE_USE_FLAGS_INVERSE, 171, 0);

    REQUIRE(normal.CanSee(inverse));
    REQUIRE(inverse.CanSee(normal));
}

TEST_CASE("PhaseShift VisibleMapId references are independent of CanSee", "[PhaseVisibility][native]")
{
    PhaseShift left = MakePhased(170);
    PhaseShift right = MakePhased(171);
    left.AddVisibleMapId(1152, nullptr);
    right.AddVisibleMapId(1153, nullptr);

    REQUIRE_FALSE(left.CanSee(right));
    REQUIRE(left.HasVisibleMapId(1152));
    REQUIRE(right.HasVisibleMapId(1153));
}

TEST_CASE("VisibleMapId reference counting add/remove once", "[PhaseVisibility][terrain]")
{
    PhaseShift shift;
    REQUIRE(shift.AddVisibleMapId(1945, nullptr));
    REQUIRE(shift.HasVisibleMapId(1945));

    // Second add increases references but stays present.
    REQUIRE_FALSE(shift.AddVisibleMapId(1945, nullptr));
    REQUIRE(shift.HasVisibleMapId(1945));

    auto first = shift.RemoveVisibleMapId(1945);
    REQUIRE_FALSE(first.Erased);
    REQUIRE(shift.HasVisibleMapId(1945));

    auto second = shift.RemoveVisibleMapId(1945);
    REQUIRE(second.Erased);
    REQUIRE_FALSE(shift.HasVisibleMapId(1945));
}

TEST_CASE("Logical exclusive context truth table", "[PhaseVisibility][logical]")
{
    REQUIRE(RoleplayPhaseMgr::SharesExclusiveContext(0, 0));
    REQUIRE(RoleplayPhaseMgr::SharesExclusiveContext(42, 42));
    REQUIRE_FALSE(RoleplayPhaseMgr::SharesExclusiveContext(0, 42));
    REQUIRE_FALSE(RoleplayPhaseMgr::SharesExclusiveContext(42, 43));
}

TEST_CASE("Public and private RP phase discovery truth table", "[PhaseVisibility][logical]")
{
    REQUIRE(RoleplayPhaseMgr::CanDiscoverUsablePhase(true, true, false));
    REQUIRE(RoleplayPhaseMgr::CanDiscoverUsablePhase(false, true, true));
    REQUIRE_FALSE(RoleplayPhaseMgr::CanDiscoverUsablePhase(false, true, false));
    REQUIRE_FALSE(RoleplayPhaseMgr::CanDiscoverUsablePhase(true, false, false));
    REQUIRE_FALSE(RoleplayPhaseMgr::CanDiscoverUsablePhase(false, false, true));
}

TEST_CASE("Teleport-only terrain preset is not a VisibleMapSwap", "[PhaseVisibility][terrain]")
{
    TerrainPreset const* teleport = GetTerrainPreset(TerrainPresetId::ArathiHighlandsRpeTeleport);
    REQUIRE(teleport);
    REQUIRE(teleport->MapId == 2927);
    REQUIRE(teleport->VisibleMapId == 0);
    REQUIRE_FALSE(teleport->IsVisibleMapSwap());

    TerrainPreset const* swap = GetTerrainPreset(TerrainPresetId::Stromgarde);
    REQUIRE(swap);
    REQUIRE(swap->IsVisibleMapSwap());
    REQUIRE(swap->VisibleMapId == 1945);
}
