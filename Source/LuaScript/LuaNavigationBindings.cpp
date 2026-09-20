//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"
#include "LuaBindMacros.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/Navigation/CrowdAgent.h"
#include "../Urho3D/Navigation/CrowdManager.h"
#include "../Urho3D/Navigation/DynamicNavigationMesh.h"
#include "../Urho3D/Navigation/Navigable.h"
#include "../Urho3D/Navigation/NavigationMesh.h"
#include "../Urho3D/Navigation/Obstacle.h"
#include "../Urho3D/Navigation/OffMeshConnection.h"
#include "../Urho3D/Scene/Node.h"

#include <sol/sol.hpp>

namespace sol
{

template <> struct is_automagical<Urho3D::NavigationMesh> : std::false_type {};
template <> struct is_automagical<Urho3D::DynamicNavigationMesh> : std::false_type {};
template <> struct is_automagical<Urho3D::Navigable> : std::false_type {};
template <> struct is_automagical<Urho3D::OffMeshConnection> : std::false_type {};
template <> struct is_automagical<Urho3D::Obstacle> : std::false_type {};
template <> struct is_automagical<Urho3D::CrowdAgent> : std::false_type {};
template <> struct is_automagical<Urho3D::CrowdManager> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterNavigationBindings(sol::state& lua, Context* context)
{
    // NavigationMesh: agent config + path queries. The mesh rebuilds itself
    // from scene geometry, so only configuration and queries are needed.
    {
        using RBFX_THIS = NavigationMesh;
        RBFX_USERTYPE(NavigationMesh, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetTileSize)
            RBFX_M(SetCellSize)
            RBFX_M(SetCellHeight)
            RBFX_M(SetAgentHeight)
            RBFX_M(SetAgentRadius)
            RBFX_M(SetAgentMaxClimb)
            RBFX_M(SetAgentMaxSlope)
            RBFX_M(SetPadding)
            // Debug draw toggles (39_CrowdNavigation).
            RBFX_M(SetDrawOffMeshConnections)
            // Build / streaming control (15_Navigation).
            RBFX_RAW(Rebuild, [](NavigationMesh* nav) -> bool { return nav && nav->Rebuild(); })
            RBFX_RAW(Allocate, [](NavigationMesh* nav) -> bool { return nav && nav->Allocate(); })
            RBFX_RAW(BuildTilesInRegion, [](NavigationMesh* nav, const BoundingBox& box) -> bool {
                return nav && nav->BuildTilesInRegion(box);
            })
            RBFX_RAW(DrawDebugGeometry, [](NavigationMesh* nav, bool depthTest) {
                if (nav)
                    nav->DrawDebugGeometry(depthTest);
            })
            RBFX_RAW(FindNearestPoint, [](NavigationMesh* nav, const Vector3& point, sol::optional<Vector3> extents) {
                return nav ? nav->FindNearestPoint(point, extents.value_or(Vector3::ONE)) : Vector3::ZERO;
            })
            // Path query returning an array of waypoints.
            RBFX_RAW(FindPath, [](NavigationMesh* nav, const Vector3& start, const Vector3& end,
                sol::this_state s) -> sol::table {
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                if (!nav)
                    return result;
                ea::vector<Vector3> dest;
                nav->FindPath(dest, start, end);
                unsigned index = 1;
                for (const Vector3& point : dest)
                    result[index++] = point;
                return result;
            })
            // Tile-level streaming API. Tile data crosses the boundary as a Lua
            // array of byte values.
            RBFX_M(GetTileIndex)
            RBFX_M(HasTile)
            RBFX_RAW(RemoveTile, [](NavigationMesh* nav, const IntVector2& tileIndex) {
                if (nav)
                    nav->RemoveTile(tileIndex);
            })
            RBFX_RAW(GetAllTileIndices, [](NavigationMesh* nav, sol::this_state s) -> sol::object {
                if (!nav)
                    return sol::lua_nil;
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                unsigned index = 1;
                for (const IntVector2& tile : nav->GetAllTileIndices())
                    result[index++] = tile;
                return result;
            })
            RBFX_RAW(GetTileData, [](NavigationMesh* nav, const IntVector2& tileIndex, sol::this_state s) -> sol::object {
                if (!nav)
                    return sol::lua_nil;
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                const ea::vector<unsigned char> data = nav->GetTileData(tileIndex);
                for (unsigned i = 0; i < data.size(); ++i)
                    result[i + 1] = data[i];
                return result;
            })
            RBFX_RAW(AddTile, [](NavigationMesh* nav, sol::table data) -> bool {
                if (!nav)
                    return false;
                ea::vector<unsigned char> bytes;
                bytes.reserve(data.size());
                for (unsigned i = 1; i <= data.size(); ++i)
                    bytes.push_back(static_cast<unsigned char>(data[i].get<int>()));
                return nav->AddTile(bytes);
            })
        );
    }
    RegisterLuaObjectWrapper<NavigationMesh>();

    // DynamicNavigationMesh: supports obstacles at runtime.
    {
        using RBFX_THIS = DynamicNavigationMesh;
        RBFX_USERTYPE(DynamicNavigationMesh, sol::no_constructor
            RBFX_BASES(NavigationMesh, Component, Serializable, Object)
            RBFX_M(SetDrawObstacles)
        );
    }
    RegisterLuaObjectWrapper<DynamicNavigationMesh>();

    // Navigable: flags a subtree as nav geometry source.
    {
        using RBFX_THIS = Navigable;
        RBFX_USERTYPE(Navigable, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Navigable>();

    // OffMeshConnection: jumps and teleports across gaps.
    {
        using RBFX_THIS = OffMeshConnection;
        RBFX_USERTYPE(OffMeshConnection, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetRadius)
            RBFX_M(SetBidirectional)
            RBFX_M(SetMask)
            // Target node of the jump (39_CrowdNavigation box climbing).
            RBFX_M(SetEndPoint)
            RBFX_M(GetEndPoint)
        );
    }
    RegisterLuaObjectWrapper<OffMeshConnection>();

    // Obstacle: dynamic blocker carved out of the DynamicNavigationMesh
    // (39_CrowdNavigation mushrooms).
    {
        using RBFX_THIS = Obstacle;
        RBFX_USERTYPE(Obstacle, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetRadius)
            RBFX_M(SetHeight)
            RBFX_M(GetRadius)
            RBFX_M(GetHeight)
        );
    }
    RegisterLuaObjectWrapper<Obstacle>();

    // CrowdAgent: agent inside a CrowdManager crowd (39_CrowdNavigation).
    {
        using RBFX_THIS = CrowdAgent;
        RBFX_USERTYPE(CrowdAgent, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetTargetPosition)
            RBFX_M(SetTargetVelocity)
            RBFX_M(SetMaxAccel)
            RBFX_M(SetMaxSpeed)
            RBFX_M(SetRadius)
            RBFX_M(SetHeight)
            RBFX_M(SetQueryFilterType)
            RBFX_M(SetObstacleAvoidanceType)
            RBFX_M_ENUM(SetNavigationQuality, NavigationQuality)
            RBFX_RAW(GetNavigationQuality, [](CrowdAgent* agent) {
                return agent ? static_cast<int>(agent->GetNavigationQuality()) : 0;
            })
            RBFX_M(GetTargetPosition)
            RBFX_M(GetDesiredVelocity)
            RBFX_M(GetActualVelocity)
            RBFX_RAW(GetAgentState, [](CrowdAgent* agent) {
                return agent ? static_cast<int>(agent->GetAgentState()) : 0;
            })
            RBFX_M(GetMaxAccel)
            RBFX_M(GetMaxSpeed)
            RBFX_M(GetRadius)
            RBFX_M(GetQueryFilterType)
        );
    }
    RegisterLuaObjectWrapper<CrowdAgent>();

    {
        using RBFX_THIS = CrowdManager;
        RBFX_USERTYPE(CrowdManager, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(SetCrowdTarget)
            RBFX_M(SetCrowdVelocity)
            RBFX_M(ResetCrowdTarget)
            RBFX_M(SetMaxAgents)
            RBFX_M(SetMaxAgentRadius)
            RBFX_M(GetMaxAgents)
            RBFX_RAW(DrawDebugGeometry, [](CrowdManager* manager, bool depthTest) {
                if (manager)
                    manager->DrawDebugGeometry(depthTest);
            })
            // Obstacle avoidance tuning, passed as flat Lua tables
            // (39_CrowdNavigation InitCrowdParams).
            RBFX_RAW(GetObstacleAvoidanceParams, [](CrowdManager* manager, unsigned type, sol::this_state s) -> sol::table {
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                if (manager)
                {
                    const CrowdObstacleAvoidanceParams& params = manager->GetObstacleAvoidanceParams(type);
                    result["velBias"] = params.velBias;
                    result["weightDesVel"] = params.weightDesVel;
                    result["weightCurVel"] = params.weightCurVel;
                    result["weightSide"] = params.weightSide;
                    result["weightToi"] = params.weightToi;
                    result["horizTime"] = params.horizTime;
                    result["gridSize"] = params.gridSize;
                    result["adaptiveDivs"] = params.adaptiveDivs;
                    result["adaptiveRings"] = params.adaptiveRings;
                    result["adaptiveDepth"] = params.adaptiveDepth;
                }
                return result;
            })
            RBFX_RAW(SetObstacleAvoidanceParams, [](CrowdManager* manager, unsigned type, const sol::table& table) {
                if (!manager)
                    return;
                CrowdObstacleAvoidanceParams params = manager->GetObstacleAvoidanceParams(type);
                params.velBias = table.get_or("velBias", params.velBias);
                params.weightDesVel = table.get_or("weightDesVel", params.weightDesVel);
                params.weightCurVel = table.get_or("weightCurVel", params.weightCurVel);
                params.weightSide = table.get_or("weightSide", params.weightSide);
                params.weightToi = table.get_or("weightToi", params.weightToi);
                params.horizTime = table.get_or("horizTime", params.horizTime);
                params.gridSize = static_cast<unsigned char>(table.get_or("gridSize", static_cast<int>(params.gridSize)));
                params.adaptiveDivs = static_cast<unsigned char>(table.get_or("adaptiveDivs", static_cast<int>(params.adaptiveDivs)));
                params.adaptiveRings = static_cast<unsigned char>(table.get_or("adaptiveRings", static_cast<int>(params.adaptiveRings)));
                params.adaptiveDepth = static_cast<unsigned char>(table.get_or("adaptiveDepth", static_cast<int>(params.adaptiveDepth)));
                manager->SetObstacleAvoidanceParams(type, params);
            })
            // Random reachable point near a position (39_CrowdNavigation
            // wandering mushrooms).
            RBFX_RAW(GetRandomPointInCircle, [](CrowdManager* manager, const Vector3& center, float radius,
                sol::optional<int> queryFilterType) -> Vector3 {
                return manager
                    ? manager->GetRandomPointInCircle(center, radius, queryFilterType.value_or(0))
                    : Vector3::ZERO;
            })
        );
    }
    RegisterLuaObjectWrapper<CrowdManager>();

    // Crowd navigation enums (39_CrowdNavigation).
    RBFX_ENUM_TABLE(NAV_QUALITY, "LOW", NAVIGATIONQUALITY_LOW, "MEDIUM", NAVIGATIONQUALITY_MEDIUM,
        "HIGH", NAVIGATIONQUALITY_HIGH);

    RBFX_ENUM_TABLE(CROWD_STATE, "INVALID", CA_STATE_INVALID, "WALKING", CA_STATE_WALKING,
        "OFFMESH", CA_STATE_OFFMESH);
}

} // namespace Urho3D
