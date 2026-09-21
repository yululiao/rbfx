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
        using LUA_THIS = NavigationMesh;
        LUA_CLASS(NavigationMesh, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetTileSize)
            LUA_MEMBER_FUNC(SetCellSize)
            LUA_MEMBER_FUNC(SetCellHeight)
            LUA_MEMBER_FUNC(SetAgentHeight)
            LUA_MEMBER_FUNC(SetAgentRadius)
            LUA_MEMBER_FUNC(SetAgentMaxClimb)
            LUA_MEMBER_FUNC(SetAgentMaxSlope)
            LUA_MEMBER_FUNC(SetPadding)
            // Debug draw toggles (39_CrowdNavigation).
            LUA_MEMBER_FUNC(SetDrawOffMeshConnections)
            // Build / streaming control (15_Navigation).
            LUA_MEMBER_FUNC_RAW(Rebuild, [](NavigationMesh* nav) -> bool { return nav && nav->Rebuild(); })
            LUA_MEMBER_FUNC_RAW(Allocate, [](NavigationMesh* nav) -> bool { return nav && nav->Allocate(); })
            LUA_MEMBER_FUNC_RAW(BuildTilesInRegion, [](NavigationMesh* nav, const BoundingBox& box) -> bool {
                return nav && nav->BuildTilesInRegion(box);
            })
            LUA_MEMBER_FUNC_RAW(DrawDebugGeometry, [](NavigationMesh* nav, bool depthTest) {
                if (nav)
                    nav->DrawDebugGeometry(depthTest);
            })
            LUA_MEMBER_FUNC_RAW(FindNearestPoint, [](NavigationMesh* nav, const Vector3& point, sol::optional<Vector3> extents) {
                return nav ? nav->FindNearestPoint(point, extents.value_or(Vector3::ONE)) : Vector3::ZERO;
            })
            // Path query returning an array of waypoints.
            LUA_MEMBER_FUNC_RAW(FindPath, [](NavigationMesh* nav, const Vector3& start, const Vector3& end,
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
            LUA_MEMBER_FUNC(GetTileIndex)
            LUA_MEMBER_FUNC(HasTile)
            LUA_MEMBER_FUNC_RAW(RemoveTile, [](NavigationMesh* nav, const IntVector2& tileIndex) {
                if (nav)
                    nav->RemoveTile(tileIndex);
            })
            LUA_MEMBER_FUNC_RAW(GetAllTileIndices, [](NavigationMesh* nav, sol::this_state s) -> sol::object {
                if (!nav)
                    return sol::lua_nil;
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                unsigned index = 1;
                for (const IntVector2& tile : nav->GetAllTileIndices())
                    result[index++] = tile;
                return result;
            })
            LUA_MEMBER_FUNC_RAW(GetTileData, [](NavigationMesh* nav, const IntVector2& tileIndex, sol::this_state s) -> sol::object {
                if (!nav)
                    return sol::lua_nil;
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                const ea::vector<unsigned char> data = nav->GetTileData(tileIndex);
                for (unsigned i = 0; i < data.size(); ++i)
                    result[i + 1] = data[i];
                return result;
            })
            LUA_MEMBER_FUNC_RAW(AddTile, [](NavigationMesh* nav, sol::table data) -> bool {
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
        using LUA_THIS = DynamicNavigationMesh;
        LUA_CLASS(DynamicNavigationMesh, sol::no_constructor
            LUA_BASES(NavigationMesh, Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetDrawObstacles)
        );
    }
    RegisterLuaObjectWrapper<DynamicNavigationMesh>();

    // Navigable: flags a subtree as nav geometry source.
    {
        using LUA_THIS = Navigable;
        LUA_CLASS(Navigable, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
        );
    }
    RegisterLuaObjectWrapper<Navigable>();

    // OffMeshConnection: jumps and teleports across gaps.
    {
        using LUA_THIS = OffMeshConnection;
        LUA_CLASS(OffMeshConnection, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetRadius)
            LUA_MEMBER_FUNC(SetBidirectional)
            LUA_MEMBER_FUNC(SetMask)
            // Target node of the jump (39_CrowdNavigation box climbing).
            LUA_MEMBER_FUNC(SetEndPoint)
            LUA_MEMBER_FUNC(GetEndPoint)
        );
    }
    RegisterLuaObjectWrapper<OffMeshConnection>();

    // Obstacle: dynamic blocker carved out of the DynamicNavigationMesh
    // (39_CrowdNavigation mushrooms).
    {
        using LUA_THIS = Obstacle;
        LUA_CLASS(Obstacle, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetRadius)
            LUA_MEMBER_FUNC(SetHeight)
            LUA_MEMBER_FUNC(GetRadius)
            LUA_MEMBER_FUNC(GetHeight)
        );
    }
    RegisterLuaObjectWrapper<Obstacle>();

    // CrowdAgent: agent inside a CrowdManager crowd (39_CrowdNavigation).
    {
        using LUA_THIS = CrowdAgent;
        LUA_CLASS(CrowdAgent, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetTargetPosition)
            LUA_MEMBER_FUNC(SetTargetVelocity)
            LUA_MEMBER_FUNC(SetMaxAccel)
            LUA_MEMBER_FUNC(SetMaxSpeed)
            LUA_MEMBER_FUNC(SetRadius)
            LUA_MEMBER_FUNC(SetHeight)
            LUA_MEMBER_FUNC(SetQueryFilterType)
            LUA_MEMBER_FUNC(SetObstacleAvoidanceType)
            LUA_MEMBER_FUNC_ENUM(SetNavigationQuality, NavigationQuality)
            LUA_MEMBER_FUNC_RAW(GetNavigationQuality, [](CrowdAgent* agent) {
                return agent ? static_cast<int>(agent->GetNavigationQuality()) : 0;
            })
            LUA_MEMBER_FUNC(GetTargetPosition)
            LUA_MEMBER_FUNC(GetDesiredVelocity)
            LUA_MEMBER_FUNC(GetActualVelocity)
            LUA_MEMBER_FUNC_RAW(GetAgentState, [](CrowdAgent* agent) {
                return agent ? static_cast<int>(agent->GetAgentState()) : 0;
            })
            LUA_MEMBER_FUNC(GetMaxAccel)
            LUA_MEMBER_FUNC(GetMaxSpeed)
            LUA_MEMBER_FUNC(GetRadius)
            LUA_MEMBER_FUNC(GetQueryFilterType)
        );
    }
    RegisterLuaObjectWrapper<CrowdAgent>();

    {
        using LUA_THIS = CrowdManager;
        LUA_CLASS(CrowdManager, sol::no_constructor
            LUA_BASES(Component, Serializable, Object)
            LUA_MEMBER_FUNC(SetCrowdTarget)
            LUA_MEMBER_FUNC(SetCrowdVelocity)
            LUA_MEMBER_FUNC(ResetCrowdTarget)
            LUA_MEMBER_FUNC(SetMaxAgents)
            LUA_MEMBER_FUNC(SetMaxAgentRadius)
            LUA_MEMBER_FUNC(GetMaxAgents)
            LUA_MEMBER_FUNC_RAW(DrawDebugGeometry, [](CrowdManager* manager, bool depthTest) {
                if (manager)
                    manager->DrawDebugGeometry(depthTest);
            })
            // Obstacle avoidance tuning, passed as flat Lua tables
            // (39_CrowdNavigation InitCrowdParams).
            LUA_MEMBER_FUNC_RAW(GetObstacleAvoidanceParams, [](CrowdManager* manager, unsigned type, sol::this_state s) -> sol::table {
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
            LUA_MEMBER_FUNC_RAW(SetObstacleAvoidanceParams, [](CrowdManager* manager, unsigned type, const sol::table& table) {
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
            LUA_MEMBER_FUNC_RAW(GetRandomPointInCircle, [](CrowdManager* manager, const Vector3& center, float radius,
                sol::optional<int> queryFilterType) -> Vector3 {
                return manager
                    ? manager->GetRandomPointInCircle(center, radius, queryFilterType.value_or(0))
                    : Vector3::ZERO;
            })
        );
    }
    RegisterLuaObjectWrapper<CrowdManager>();

    // Crowd navigation enums (39_CrowdNavigation).
    LUA_ENUM_TABLE(NAV_QUALITY, "LOW", NAVIGATIONQUALITY_LOW, "MEDIUM", NAVIGATIONQUALITY_MEDIUM,
        "HIGH", NAVIGATIONQUALITY_HIGH);

    LUA_ENUM_TABLE(CROWD_STATE, "INVALID", CA_STATE_INVALID, "WALKING", CA_STATE_WALKING,
        "OFFMESH", CA_STATE_OFFMESH);
}

} // namespace Urho3D
