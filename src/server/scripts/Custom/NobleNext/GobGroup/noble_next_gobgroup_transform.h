/*
 * NobleNext — spatial GO group transform math + batch SQL writer.
 *
 * Relative poses use a full root quaternion frame:
 *   localXYZ = inv(rootRot) * (memberXYZ - rootXYZ)
 *   RelativeRotation = memberRot * inv(rootRot)
 * Legacy yaw-only captured offsets remain valid while the root is yaw-aligned;
 * `.gobject group recalc` upgrades rows to the full-quat representation.
 */

#pragma once

#include "Define.h"
#include "GridDefines.h"
#include "ObjectGuid.h"
#include "Position.h"
#include "QuaternionData.h"

#include "DatabaseEnvFwd.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RoleplayCore::NobleNext
{
    static constexpr uint32 GOBGROUP_MAX_MEMBERS = 128;
    static constexpr uint32 GOBGROUP_SQL_CHUNK_SIZE = 32;
    static constexpr uint32 GOBGROUP_RUNTIME_CHUNK_SIZE = 16;
    static constexpr uint32 GOBGROUP_RUNTIME_BUDGET_MS = 2;
    static constexpr float GOBGROUP_POS_EPSILON = 0.05f;
    static constexpr float GOBGROUP_ORI_EPSILON = 0.02f;
    static constexpr float GOBGROUP_QUAT_EPSILON = 0.001f;

    struct MemberRelativeTransform
    {
        float OffsetX = 0.f;
        float OffsetY = 0.f;
        float OffsetZ = 0.f;
        float OffsetO = 0.f;
        QuaternionData RelativeRotation;
    };

    struct GroupTransformRow
    {
        ObjectGuid::LowType SpawnId = 0;
        Position OldWorld;
        Position NewWorld;
        QuaternionData OldRotation;
        QuaternionData NewRotation;
        CellCoord OldCell{ 0, 0 };
        CellCoord NewCell{ 0, 0 };
        uint32 OldMapId = 0;
        uint32 NewMapId = 0;
    };

    struct GroupTransformPlan
    {
        ObjectGuid::LowType RootGuid = 0;
        uint32 SourceMapId = 0;
        uint32 TargetMapId = 0;
        bool CrossMap = false;
        std::vector<GroupTransformRow> Rows; // root first, then members ASC
    };

    namespace GobGroupTransform
    {
        QuaternionData Unitize(QuaternionData const& q);
        bool IsFinite(QuaternionData const& q);
        bool IsFinite(Position const& pos);
        bool QuatNearlyEqual(QuaternionData const& a, QuaternionData const& b, float eps = GOBGROUP_QUAT_EPSILON);
        bool PosNearlyEqual(Position const& a, Position const& b,
            float posEps = GOBGROUP_POS_EPSILON, float oriEps = GOBGROUP_ORI_EPSILON);

        QuaternionData YawQuat(float orientation);

        // Full-quaternion relative rotation: member * inv(root)
        QuaternionData ComputeRelativeRotation(QuaternionData const& memberWorld, QuaternionData const& rootWorld);
        // Full-quaternion apply: relative * newRoot
        QuaternionData ApplyRelativeRotation(QuaternionData const& relative, QuaternionData const& newRootWorld);

        // Thin yaw wrappers (legacy / yaw-aligned roots)
        QuaternionData ComputeRelativeRotation(QuaternionData const& memberWorld, float rootOrientation);
        QuaternionData ApplyRelativeRotation(QuaternionData const& relative, float newRootOrientation);

        QuaternionData ApplyRootTilt(QuaternionData const& rootWorld, float oldRootOrientation, float newRootOrientation);

        MemberRelativeTransform ComputeRelative(Position const& rootPos, QuaternionData const& rootRot,
            Position const& memberPos, QuaternionData const& memberRot);

        Position ApplyLocalOffset(Position const& rootPos, QuaternionData const& rootRot,
            MemberRelativeTransform const& local);
        Position ApplyLocalOffsetDouble(Position const& rootPos, QuaternionData const& rootRot,
            MemberRelativeTransform const& local);

        // Thin yaw wrappers
        Position ApplyLocalOffset(Position const& rootPos, MemberRelativeTransform const& local);
        Position ApplyLocalOffsetDouble(Position const& rootPos, MemberRelativeTransform const& local);

        bool BuildPlan(ObjectGuid::LowType rootGuid, uint32 sourceMapId, uint32 targetMapId,
            Position const& newRootPos, QuaternionData const& newRootRotation,
            std::vector<ObjectGuid::LowType> const& memberOrder,
            std::unordered_map<ObjectGuid::LowType, MemberRelativeTransform> const& relatives,
            std::unordered_map<ObjectGuid::LowType, Position> const& oldPositions,
            std::unordered_map<ObjectGuid::LowType, QuaternionData> const& oldRotations,
            GroupTransformPlan& outPlan, std::string& error);

        bool ValidatePlan(GroupTransformPlan const& plan, std::string& error);

        std::string FormatSqlFloat(double value);
        void AppendTransformUpdateChunks(WorldDatabaseTransaction trans, GroupTransformPlan const& plan, bool updateMap);
        void AppendRelativeUpsertChunks(WorldDatabaseTransaction trans, ObjectGuid::LowType rootGuid,
            std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> const& rows);
    }
}
