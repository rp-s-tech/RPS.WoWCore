/*
 * Pure access-policy truth table for logical RP phases and command guards.
 */

#include "RoleplayCommandPhaseGuard.h"
#include "RoleplayPhaseMgr.h"

#include "catch2/catch_test_macros.hpp"

TEST_CASE("Logical exclusive context truth table for command guards", "[PhaseVisibility][commands][logical]")
{
    REQUIRE(RoleplayPhaseMgr::SharesExclusiveContext(0, 0));
    REQUIRE(RoleplayPhaseMgr::SharesExclusiveContext(7, 7));
    REQUIRE_FALSE(RoleplayPhaseMgr::SharesExclusiveContext(0, 7));
    REQUIRE_FALSE(RoleplayPhaseMgr::SharesExclusiveContext(7, 8));
}

TEST_CASE("Command phase context allows only same logical id without bypass", "[PhaseVisibility][commands]")
{
    RoleplayCommandPhaseGuard::SpawnContext same;
    same.ViewerPhaseId = 0;
    same.SpawnPhaseId = 0;
    same.CrossPhase = false;

    RoleplayCommandPhaseGuard::SpawnContext foreign;
    foreign.ViewerPhaseId = 1;
    foreign.SpawnPhaseId = 2;
    foreign.CrossPhase = true;

    RoleplayCommandPhaseGuard::SpawnContext commonVsA;
    commonVsA.ViewerPhaseId = 0;
    commonVsA.SpawnPhaseId = 5;
    commonVsA.CrossPhase = true;

    REQUIRE(RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(same, false));
    REQUIRE(RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(same, true));
    REQUIRE_FALSE(RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(foreign, false));
    REQUIRE(RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(foreign, true));
    REQUIRE_FALSE(RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(commonVsA, false));
    REQUIRE(RoleplayCommandPhaseGuard::AllowsViewerSpawnContext(commonVsA, true));
}

TEST_CASE("Phase tag formatting stays decimal and stable", "[PhaseVisibility][commands]")
{
    REQUIRE(RoleplayCommandPhaseGuard::FormatPhaseTag(0) == "[RP phase: 0]");
    REQUIRE(RoleplayCommandPhaseGuard::FormatPhaseTag(42) == "[RP phase: 42]");
    REQUIRE(RoleplayCommandPhaseGuard::FormatPhaseTag(UINT64_C(9007199254740993)) == "[RP phase: 9007199254740993]");
}
