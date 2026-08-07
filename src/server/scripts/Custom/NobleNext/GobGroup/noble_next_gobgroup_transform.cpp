/*
 * NobleNext — spatial GO group transform math + batch SQL writer.
 */

#include "noble_next_gobgroup_transform.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "MapManager.h"
#include "StringFormat.h"

#include <G3D/Matrix3.h>
#include <G3D/Quat.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace RoleplayCore::NobleNext
{
namespace GobGroupTransform
{
    namespace
    {
        G3D::Quat ToG3D(QuaternionData const& q)
        {
            return G3D::Quat(q.x, q.y, q.z, q.w);
        }

        QuaternionData FromG3D(G3D::Quat q)
        {
            q.unitize();
            return QuaternionData(float(q.x), float(q.y), float(q.z), float(q.w));
        }
    }

    QuaternionData Unitize(QuaternionData const& q)
    {
        return FromG3D(ToG3D(q));
    }

    bool IsFinite(QuaternionData const& q)
    {
        return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
    }

    bool IsFinite(Position const& pos)
    {
        return std::isfinite(pos.GetPositionX()) && std::isfinite(pos.GetPositionY())
            && std::isfinite(pos.GetPositionZ()) && std::isfinite(pos.GetOrientation());
    }

    bool QuatNearlyEqual(QuaternionData const& a, QuaternionData const& b, float eps)
    {
        auto close = [&](float sign)
        {
            return std::fabs(a.x - sign * b.x) <= eps
                && std::fabs(a.y - sign * b.y) <= eps
                && std::fabs(a.z - sign * b.z) <= eps
                && std::fabs(a.w - sign * b.w) <= eps;
        };
        return close(1.f) || close(-1.f);
    }

    bool PosNearlyEqual(Position const& a, Position const& b, float posEps, float oriEps)
    {
        if (a.GetExactDist(b) > posEps)
            return false;

        float dO = Position::NormalizeOrientation(a.GetOrientation() - b.GetOrientation());
        if (dO > float(M_PI))
            dO = float(2.0 * M_PI) - dO;
        return dO <= oriEps;
    }

    QuaternionData YawQuat(float orientation)
    {
        return FromG3D(G3D::Quat(G3D::Matrix3::fromEulerAnglesZYX(orientation, 0.0f, 0.0f)));
    }

    QuaternionData ComputeRelativeRotation(QuaternionData const& memberWorld, float rootOrientation)
    {
        G3D::Quat const rootYaw = ToG3D(YawQuat(rootOrientation));
        G3D::Quat const relative = ToG3D(memberWorld) * rootYaw.inverse();
        return FromG3D(relative);
    }

    QuaternionData ApplyRelativeRotation(QuaternionData const& relative, float newRootOrientation)
    {
        G3D::Quat const world = ToG3D(relative) * ToG3D(YawQuat(newRootOrientation));
        return FromG3D(world);
    }

    QuaternionData ApplyRootTilt(QuaternionData const& rootWorld, float oldRootOrientation, float newRootOrientation)
    {
        G3D::Quat const tilt = ToG3D(rootWorld) * ToG3D(YawQuat(oldRootOrientation)).inverse();
        return FromG3D(tilt * ToG3D(YawQuat(newRootOrientation)));
    }

    MemberRelativeTransform ComputeRelative(Position const& rootPos, QuaternionData const& /*rootRot*/,
        Position const& memberPos, QuaternionData const& memberRot)
    {
        Position const local = rootPos.GetPositionOffsetTo(memberPos);
        MemberRelativeTransform rel;
        rel.OffsetX = local.GetPositionX();
        rel.OffsetY = local.GetPositionY();
        rel.OffsetZ = local.GetPositionZ();
        rel.OffsetO = local.GetOrientation();
        rel.RelativeRotation = ComputeRelativeRotation(memberRot, rootPos.GetOrientation());
        return rel;
    }

    Position ApplyLocalOffset(Position const& rootPos, MemberRelativeTransform const& local)
    {
        Position offset(local.OffsetX, local.OffsetY, local.OffsetZ, local.OffsetO);
        return rootPos.GetPositionWithOffset(offset);
    }

    Position ApplyLocalOffsetDouble(Position const& rootPos, MemberRelativeTransform const& local)
    {
        double const rootO = rootPos.GetOrientation();
        double const cosO = std::cos(rootO);
        double const sinO = std::sin(rootO);
        double const worldX = double(rootPos.GetPositionX()) + double(local.OffsetX) * cosO - double(local.OffsetY) * sinO;
        double const worldY = double(rootPos.GetPositionY()) + double(local.OffsetY) * cosO + double(local.OffsetX) * sinO;
        double const worldZ = double(rootPos.GetPositionZ()) + double(local.OffsetZ);
        double const worldO = double(rootPos.GetOrientation()) + double(local.OffsetO);
        return Position(float(worldX), float(worldY), float(worldZ), Position::NormalizeOrientation(float(worldO)));
    }

    bool BuildPlan(ObjectGuid::LowType rootGuid, uint32 sourceMapId, uint32 targetMapId,
        Position const& newRootPos, QuaternionData const& newRootRotation,
        std::vector<ObjectGuid::LowType> const& memberOrder,
        std::unordered_map<ObjectGuid::LowType, MemberRelativeTransform> const& relatives,
        std::unordered_map<ObjectGuid::LowType, Position> const& oldPositions,
        std::unordered_map<ObjectGuid::LowType, QuaternionData> const& oldRotations,
        GroupTransformPlan& outPlan, std::string& error)
    {
        outPlan = GroupTransformPlan{};
        outPlan.RootGuid = rootGuid;
        outPlan.SourceMapId = sourceMapId;
        outPlan.TargetMapId = targetMapId;
        outPlan.CrossMap = sourceMapId != targetMapId;

        auto oldPosIt = oldPositions.find(rootGuid);
        auto oldRotIt = oldRotations.find(rootGuid);
        if (oldPosIt == oldPositions.end() || oldRotIt == oldRotations.end())
        {
            error = "Root spawn data missing for transform plan";
            return false;
        }

        if (!IsFinite(newRootPos) || !IsFinite(newRootRotation))
        {
            error = "New root transform is not finite";
            return false;
        }

        GroupTransformRow rootRow;
        rootRow.SpawnId = rootGuid;
        rootRow.OldWorld = oldPosIt->second;
        rootRow.NewWorld = newRootPos;
        rootRow.OldRotation = oldRotIt->second;
        rootRow.NewRotation = Unitize(newRootRotation);
        rootRow.OldMapId = sourceMapId;
        rootRow.NewMapId = targetMapId;
        rootRow.OldCell = Trinity::ComputeCellCoord(rootRow.OldWorld.GetPositionX(), rootRow.OldWorld.GetPositionY());
        rootRow.NewCell = Trinity::ComputeCellCoord(rootRow.NewWorld.GetPositionX(), rootRow.NewWorld.GetPositionY());
        outPlan.Rows.push_back(rootRow);

        double const rootO = newRootPos.GetOrientation();
        double const cosO = std::cos(rootO);
        double const sinO = std::sin(rootO);
        G3D::Quat const newRootYaw = ToG3D(YawQuat(newRootPos.GetOrientation()));

        for (ObjectGuid::LowType memberGuid : memberOrder)
        {
            auto relIt = relatives.find(memberGuid);
            auto mPosIt = oldPositions.find(memberGuid);
            auto mRotIt = oldRotations.find(memberGuid);
            if (relIt == relatives.end() || mPosIt == oldPositions.end() || mRotIt == oldRotations.end())
            {
                error = Trinity::StringFormat("Member {} missing data for transform plan", memberGuid);
                return false;
            }

            MemberRelativeTransform const& local = relIt->second;
            double const worldX = double(newRootPos.GetPositionX()) + double(local.OffsetX) * cosO - double(local.OffsetY) * sinO;
            double const worldY = double(newRootPos.GetPositionY()) + double(local.OffsetY) * cosO + double(local.OffsetX) * sinO;
            double const worldZ = double(newRootPos.GetPositionZ()) + double(local.OffsetZ);
            double const worldO = rootO + double(local.OffsetO);

            if (!std::isfinite(worldX) || !std::isfinite(worldY) || !std::isfinite(worldZ) || !std::isfinite(worldO))
            {
                error = Trinity::StringFormat("Non-finite world pose for member {}", memberGuid);
                return false;
            }

            G3D::Quat worldQuat = ToG3D(local.RelativeRotation) * newRootYaw;
            worldQuat.unitize();
            if (!std::isfinite(worldQuat.x) || !std::isfinite(worldQuat.y) || !std::isfinite(worldQuat.z) || !std::isfinite(worldQuat.w))
            {
                error = Trinity::StringFormat("Non-finite quaternion for member {}", memberGuid);
                return false;
            }

            GroupTransformRow row;
            row.SpawnId = memberGuid;
            row.OldWorld = mPosIt->second;
            row.NewWorld = Position(float(worldX), float(worldY), float(worldZ), Position::NormalizeOrientation(float(worldO)));
            row.OldRotation = mRotIt->second;
            row.NewRotation = QuaternionData(float(worldQuat.x), float(worldQuat.y), float(worldQuat.z), float(worldQuat.w));
            row.OldMapId = sourceMapId;
            row.NewMapId = targetMapId;
            row.OldCell = Trinity::ComputeCellCoord(row.OldWorld.GetPositionX(), row.OldWorld.GetPositionY());
            row.NewCell = Trinity::ComputeCellCoord(row.NewWorld.GetPositionX(), row.NewWorld.GetPositionY());
            outPlan.Rows.push_back(row);
        }

        if (!ValidatePlan(outPlan, error))
            return false;

        return true;
    }

    bool ValidatePlan(GroupTransformPlan const& plan, std::string& error)
    {
        if (plan.Rows.empty())
        {
            error = "Empty transform plan";
            return false;
        }

        if (plan.Rows.size() > GOBGROUP_MAX_MEMBERS + 1)
        {
            error = "Transform plan exceeds max group size";
            return false;
        }

        std::unordered_map<ObjectGuid::LowType, bool> seen;
        for (GroupTransformRow const& row : plan.Rows)
        {
            if (!seen.emplace(row.SpawnId, true).second)
            {
                error = Trinity::StringFormat("Duplicate spawn {} in transform plan", row.SpawnId);
                return false;
            }

            if (!IsFinite(row.NewWorld) || !IsFinite(row.NewRotation))
            {
                error = Trinity::StringFormat("Non-finite transform for {}", row.SpawnId);
                return false;
            }

            if (!MapManager::IsValidMapCoord(row.NewMapId, row.NewWorld))
            {
                error = Trinity::StringFormat("Invalid map coord for {} on map {}", row.SpawnId, row.NewMapId);
                return false;
            }

            QuaternionData const unit = Unitize(row.NewRotation);
            if (!QuatNearlyEqual(unit, row.NewRotation, 0.01f) && !row.NewRotation.isUnit())
            {
                // soft: already unitized in BuildPlan; keep fail-closed on NaN only
            }
        }

        return true;
    }

    std::string FormatSqlFloat(double value)
    {
        if (!std::isfinite(value))
            return "0";
        return Trinity::StringFormat("{:.9g}", value);
    }

    void AppendTransformUpdateChunks(WorldDatabaseTransaction trans, GroupTransformPlan const& plan, bool updateMap)
    {
        for (size_t offset = 0; offset < plan.Rows.size(); offset += GOBGROUP_SQL_CHUNK_SIZE)
        {
            size_t const end = std::min(offset + GOBGROUP_SQL_CHUNK_SIZE, plan.Rows.size());
            std::ostringstream px, py, pz, po, r0, r1, r2, r3, mapCase, inList;
            px << "position_x = CASE guid";
            py << "position_y = CASE guid";
            pz << "position_z = CASE guid";
            po << "orientation = CASE guid";
            r0 << "rotation0 = CASE guid";
            r1 << "rotation1 = CASE guid";
            r2 << "rotation2 = CASE guid";
            r3 << "rotation3 = CASE guid";
            if (updateMap)
                mapCase << "map = CASE guid";

            bool first = true;
            for (size_t i = offset; i < end; ++i)
            {
                GroupTransformRow const& row = plan.Rows[i];
                std::string const guid = Trinity::StringFormat("{}", row.SpawnId);
                px << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewWorld.GetPositionX());
                py << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewWorld.GetPositionY());
                pz << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewWorld.GetPositionZ());
                po << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewWorld.GetOrientation());
                r0 << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewRotation.x);
                r1 << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewRotation.y);
                r2 << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewRotation.z);
                r3 << " WHEN " << guid << " THEN " << FormatSqlFloat(row.NewRotation.w);
                if (updateMap)
                    mapCase << " WHEN " << guid << " THEN " << row.NewMapId;

                if (!first)
                    inList << ',';
                inList << guid;
                first = false;
            }

            px << " END";
            py << " END";
            pz << " END";
            po << " END";
            r0 << " END";
            r1 << " END";
            r2 << " END";
            r3 << " END";
            if (updateMap)
                mapCase << " END";

            std::ostringstream sql;
            sql << "UPDATE gameobject SET "
                << px.str() << ", " << py.str() << ", " << pz.str() << ", " << po.str() << ", "
                << r0.str() << ", " << r1.str() << ", " << r2.str() << ", " << r3.str();
            if (updateMap)
                sql << ", " << mapCase.str();
            sql << " WHERE guid IN (" << inList.str() << ')';

            trans->Append(sql.str().c_str());
        }
    }

    void AppendRelativeUpsertChunks(WorldDatabaseTransaction trans, ObjectGuid::LowType rootGuid,
        std::vector<std::pair<ObjectGuid::LowType, MemberRelativeTransform>> const& rows)
    {
        for (size_t offset = 0; offset < rows.size(); offset += GOBGROUP_SQL_CHUNK_SIZE)
        {
            size_t const end = std::min(offset + GOBGROUP_SQL_CHUNK_SIZE, rows.size());
            std::ostringstream sql;
            sql << "INSERT INTO gameobject_group "
                   "(member_guid, root_guid, offset_x, offset_y, offset_z, offset_o, "
                   "rotation0, rotation1, rotation2, rotation3) VALUES ";

            for (size_t i = offset; i < end; ++i)
            {
                if (i != offset)
                    sql << ',';
                auto const& [memberGuid, rel] = rows[i];
                sql << '('
                    << memberGuid << ','
                    << rootGuid << ','
                    << FormatSqlFloat(rel.OffsetX) << ','
                    << FormatSqlFloat(rel.OffsetY) << ','
                    << FormatSqlFloat(rel.OffsetZ) << ','
                    << FormatSqlFloat(rel.OffsetO) << ','
                    << FormatSqlFloat(rel.RelativeRotation.x) << ','
                    << FormatSqlFloat(rel.RelativeRotation.y) << ','
                    << FormatSqlFloat(rel.RelativeRotation.z) << ','
                    << FormatSqlFloat(rel.RelativeRotation.w) << ')';
            }

            sql << " ON DUPLICATE KEY UPDATE "
                   "root_guid=VALUES(root_guid), "
                   "offset_x=VALUES(offset_x), offset_y=VALUES(offset_y), "
                   "offset_z=VALUES(offset_z), offset_o=VALUES(offset_o), "
                   "rotation0=VALUES(rotation0), rotation1=VALUES(rotation1), "
                   "rotation2=VALUES(rotation2), rotation3=VALUES(rotation3)";

            trans->Append(sql.str().c_str());
        }
    }
}
}
