//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"
#include "LuaBindHelpers.h"

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
    lua.new_usertype<NavigationMesh>("NavigationMesh",
        sol::no_constructor,
        sol::base_classes, LuaBases<NavigationMesh, Component, Serializable, Object>::bases(lua),
        "SetTileSize", &NavigationMesh::SetTileSize,
        "SetCellSize", &NavigationMesh::SetCellSize,
        "SetCellHeight", &NavigationMesh::SetCellHeight,
        "SetAgentHeight", &NavigationMesh::SetAgentHeight,
        "SetAgentRadius", &NavigationMesh::SetAgentRadius,
        "SetAgentMaxClimb", &NavigationMesh::SetAgentMaxClimb,
        "SetAgentMaxSlope", &NavigationMesh::SetAgentMaxSlope,
        "SetPadding", &NavigationMesh::SetPadding,
        // Debug draw toggles (39_CrowdNavigation).
        "SetDrawOffMeshConnections", &NavigationMesh::SetDrawOffMeshConnections,
        // Build / streaming control (15_Navigation).
        "Rebuild", [](NavigationMesh* nav) -> bool { return nav && nav->Rebuild(); },
        "Allocate", [](NavigationMesh* nav) -> bool { return nav && nav->Allocate(); },
        "BuildTilesInRegion", [](NavigationMesh* nav, const BoundingBox& box) -> bool {
            return nav && nav->BuildTilesInRegion(box);
        },
        "DrawDebugGeometry", [](NavigationMesh* nav, bool depthTest) {
            if (nav)
                nav->DrawDebugGeometry(depthTest);
        },
        "FindNearestPoint", [](NavigationMesh* nav, const Vector3& point, sol::optional<Vector3> extents) {
            return nav ? nav->FindNearestPoint(point, extents.value_or(Vector3::ONE)) : Vector3::ZERO;
        },
        // Path query returning an array of waypoints.
        "FindPath", [](NavigationMesh* nav, const Vector3& start, const Vector3& end,
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
        },
        // Tile-level streaming API. Tile data crosses the boundary as a Lua
        // array of byte values.
        "GetTileIndex", &NavigationMesh::GetTileIndex,
        "HasTile", &NavigationMesh::HasTile,
        "RemoveTile", [](NavigationMesh* nav, const IntVector2& tileIndex) {
            if (nav)
                nav->RemoveTile(tileIndex);
        },
        "GetAllTileIndices", [](NavigationMesh* nav, sol::this_state s) -> sol::object {
            if (!nav)
                return sol::lua_nil;
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            unsigned index = 1;
            for (const IntVector2& tile : nav->GetAllTileIndices())
                result[index++] = tile;
            return result;
        },
        "GetTileData", [](NavigationMesh* nav, const IntVector2& tileIndex, sol::this_state s) -> sol::object {
            if (!nav)
                return sol::lua_nil;
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            const ea::vector<unsigned char> data = nav->GetTileData(tileIndex);
            for (unsigned i = 0; i < data.size(); ++i)
                result[i + 1] = data[i];
            return result;
        },
        "AddTile", [](NavigationMesh* nav, sol::table data) -> bool {
            if (!nav)
                return false;
            ea::vector<unsigned char> bytes;
            bytes.reserve(data.size());
            for (unsigned i = 1; i <= data.size(); ++i)
                bytes.push_back(static_cast<unsigned char>(data[i].get<int>()));
            return nav->AddTile(bytes);
        }
    );
    RegisterLuaObjectWrapper<NavigationMesh>();

    // DynamicNavigationMesh: supports obstacles at runtime.
    lua.new_usertype<DynamicNavigationMesh>("DynamicNavigationMesh",
        sol::no_constructor,
        sol::base_classes, LuaBases<DynamicNavigationMesh, NavigationMesh, Component, Serializable, Object>::bases(lua),
        "SetDrawObstacles", &DynamicNavigationMesh::SetDrawObstacles
    );
    RegisterLuaObjectWrapper<DynamicNavigationMesh>();

    // Navigable: flags a subtree as nav geometry source.
    lua.new_usertype<Navigable>("Navigable",
        sol::no_constructor,
        sol::base_classes, LuaBases<Navigable, Component, Serializable, Object>::bases(lua)
    );
    RegisterLuaObjectWrapper<Navigable>();

    // OffMeshConnection: jumps and teleports across gaps.
    lua.new_usertype<OffMeshConnection>("OffMeshConnection",
        sol::no_constructor,
        sol::base_classes, LuaBases<OffMeshConnection, Component, Serializable, Object>::bases(lua),
        "SetRadius", &OffMeshConnection::SetRadius,
        "SetBidirectional", &OffMeshConnection::SetBidirectional,
        "SetMask", &OffMeshConnection::SetMask,
        // Target node of the jump (39_CrowdNavigation box climbing).
        "SetEndPoint", &OffMeshConnection::SetEndPoint,
        "GetEndPoint", &OffMeshConnection::GetEndPoint
    );
    RegisterLuaObjectWrapper<OffMeshConnection>();

    // Obstacle: dynamic blocker carved out of the DynamicNavigationMesh
    // (39_CrowdNavigation mushrooms).
    lua.new_usertype<Obstacle>("Obstacle",
        sol::no_constructor,
        sol::base_classes, LuaBases<Obstacle, Component, Serializable, Object>::bases(lua),
        "SetRadius", &Obstacle::SetRadius,
        "SetHeight", &Obstacle::SetHeight,
        "GetRadius", &Obstacle::GetRadius,
        "GetHeight", &Obstacle::GetHeight
    );
    RegisterLuaObjectWrapper<Obstacle>();

    // CrowdAgent: agent inside a CrowdManager crowd (39_CrowdNavigation).
    lua.new_usertype<CrowdAgent>("CrowdAgent",
        sol::no_constructor,
        sol::base_classes, LuaBases<CrowdAgent, Component, Serializable, Object>::bases(lua),
        "SetTargetPosition", &CrowdAgent::SetTargetPosition,
        "SetTargetVelocity", &CrowdAgent::SetTargetVelocity,
        "SetMaxAccel", &CrowdAgent::SetMaxAccel,
        "SetMaxSpeed", &CrowdAgent::SetMaxSpeed,
        "SetRadius", &CrowdAgent::SetRadius,
        "SetHeight", &CrowdAgent::SetHeight,
        "SetQueryFilterType", &CrowdAgent::SetQueryFilterType,
        "SetObstacleAvoidanceType", &CrowdAgent::SetObstacleAvoidanceType,
        "SetNavigationQuality", [](CrowdAgent* agent, int quality) {
            if (agent)
                agent->SetNavigationQuality(static_cast<NavigationQuality>(quality));
        },
        "GetNavigationQuality", [](CrowdAgent* agent) {
            return agent ? static_cast<int>(agent->GetNavigationQuality()) : 0;
        },
        "GetTargetPosition", &CrowdAgent::GetTargetPosition,
        "GetDesiredVelocity", &CrowdAgent::GetDesiredVelocity,
        "GetActualVelocity", &CrowdAgent::GetActualVelocity,
        "GetAgentState", [](CrowdAgent* agent) {
            return agent ? static_cast<int>(agent->GetAgentState()) : 0;
        },
        "GetMaxAccel", &CrowdAgent::GetMaxAccel,
        "GetMaxSpeed", &CrowdAgent::GetMaxSpeed,
        "GetRadius", &CrowdAgent::GetRadius,
        "GetQueryFilterType", &CrowdAgent::GetQueryFilterType
    );
    RegisterLuaObjectWrapper<CrowdAgent>();

    lua.new_usertype<CrowdManager>("CrowdManager",
        sol::no_constructor,
        sol::base_classes, LuaBases<CrowdManager, Component, Serializable, Object>::bases(lua),
        "SetCrowdTarget", &CrowdManager::SetCrowdTarget,
        "SetCrowdVelocity", &CrowdManager::SetCrowdVelocity,
        "ResetCrowdTarget", &CrowdManager::ResetCrowdTarget,
        "SetMaxAgents", &CrowdManager::SetMaxAgents,
        "SetMaxAgentRadius", &CrowdManager::SetMaxAgentRadius,
        "GetMaxAgents", &CrowdManager::GetMaxAgents,
        "DrawDebugGeometry", [](CrowdManager* manager, bool depthTest) {
            if (manager)
                manager->DrawDebugGeometry(depthTest);
        },
        // Obstacle avoidance tuning, passed as flat Lua tables
        // (39_CrowdNavigation InitCrowdParams).
        "GetObstacleAvoidanceParams", [](CrowdManager* manager, unsigned type, sol::this_state s) -> sol::table {
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
        },
        "SetObstacleAvoidanceParams", [](CrowdManager* manager, unsigned type, const sol::table& table) {
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
        },
        // Random reachable point near a position (39_CrowdNavigation
        // wandering mushrooms).
        "GetRandomPointInCircle", [](CrowdManager* manager, const Vector3& center, float radius,
            sol::optional<int> queryFilterType) -> Vector3 {
            return manager
                ? manager->GetRandomPointInCircle(center, radius, queryFilterType.value_or(0))
                : Vector3::ZERO;
        }
    );
    RegisterLuaObjectWrapper<CrowdManager>();

    // Crowd navigation enums (39_CrowdNavigation).
    sol::table navQuality = lua.create_named_table("NAV_QUALITY");
    navQuality["LOW"] = NAVIGATIONQUALITY_LOW;
    navQuality["MEDIUM"] = NAVIGATIONQUALITY_MEDIUM;
    navQuality["HIGH"] = NAVIGATIONQUALITY_HIGH;

    sol::table crowdAgentState = lua.create_named_table("CROWD_STATE");
    crowdAgentState["INVALID"] = CA_STATE_INVALID;
    crowdAgentState["WALKING"] = CA_STATE_WALKING;
    crowdAgentState["OFFMESH"] = CA_STATE_OFFMESH;
}

} // namespace Urho3D
