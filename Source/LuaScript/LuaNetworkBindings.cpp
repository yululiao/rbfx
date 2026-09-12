//
// Copyright (c) 2026 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//

#include "../Urho3D/Precompiled.h"

#include "LuaBindings.h"

#include "../Urho3D/Core/Context.h"
#include "../Urho3D/IO/MemoryBuffer.h"
#include "../Urho3D/IO/VectorBuffer.h"
#include "../Urho3D/Network/Connection.h"
#include "../Urho3D/Network/HttpRequest.h"
#include "../Urho3D/Network/LANDiscoveryManager.h"
#include "../Urho3D/Network/Network.h"
#include "../Urho3D/Network/Protocol.h"
#include "../Urho3D/Resource/JSONFile.h"
#include "../Urho3D/Replica/BehaviorNetworkObject.h"
#include "../Urho3D/Replica/ClientReplica.h"
#include "../Urho3D/Replica/NetworkObject.h"
#include "../Urho3D/Replica/ReplicationManager.h"
#include "../Urho3D/Scene/Node.h"
#include "../Urho3D/Scene/Scene.h"

#include <sol/sol.hpp>

namespace Urho3D
{

/// Lua-side holder for incoming network message data. MemoryBuffer only
/// references external memory and does not own it, so the bytes must be
/// owned alongside the reader (16_Chat, 17_SceneReplication).
struct LuaMemoryBuffer
{
    std::string data_;
    MemoryBuffer buffer_;

    explicit LuaMemoryBuffer(const std::string& bytes)
        : data_(bytes)
        , buffer_(data_.c_str(), static_cast<unsigned>(data_.size()))
    {
    }

    LuaMemoryBuffer(const LuaMemoryBuffer& rhs)
        : data_(rhs.data_)
        , buffer_(data_.c_str(), static_cast<unsigned>(data_.size()))
    {
        buffer_.Seek(rhs.buffer_.GetPosition());
    }

    LuaMemoryBuffer(LuaMemoryBuffer&& rhs)
        : data_(std::move(rhs.data_))
        , buffer_(data_.c_str(), static_cast<unsigned>(data_.size()))
    {
        buffer_.Seek(rhs.buffer_.GetPosition());
    }
};

} // namespace Urho3D

namespace sol
{

template <> struct is_automagical<Urho3D::Network> : std::false_type {};
template <> struct is_automagical<Urho3D::Connection> : std::false_type {};
template <> struct is_automagical<Urho3D::VectorBuffer> : std::false_type {};
template <> struct is_automagical<Urho3D::LuaMemoryBuffer> : std::false_type {};
template <> struct is_automagical<Urho3D::ReplicationManager> : std::false_type {};
template <> struct is_automagical<Urho3D::ClientReplica> : std::false_type {};
template <> struct is_automagical<Urho3D::NetworkObject> : std::false_type {};
template <> struct is_automagical<Urho3D::BehaviorNetworkObject> : std::false_type {};
template <> struct is_automagical<Urho3D::HttpRequest> : std::false_type {};
template <> struct is_automagical<Urho3D::JSONFile> : std::false_type {};
template <> struct is_automagical<Urho3D::JSONValue> : std::false_type {};
template <> struct is_automagical<Urho3D::LANDiscoveryManager> : std::false_type {};

} // namespace sol

namespace Urho3D
{

void RegisterNetworkBindings(sol::state& lua, Context* context)
{
    // Network subsystem. rbfx addresses endpoints through the URL type; the
    // bindings accept a "host:port" string or a bare port number.
    lua.new_usertype<Network>("Network",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "Connect", [](Network* network, const char* url, Scene* scene) -> bool {
            return network && network->Connect(URL(url), scene);
        },
        "StartServer", [](Network* network, unsigned short port) -> bool {
            return network && network->StartServer(URL(port));
        },
        "Disconnect", [](Network* network) {
            if (network)
                network->Disconnect();
        },
        "GetServerConnection", &Network::GetServerConnection,
        "GetClientConnections", [](Network* network, sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();
            if (network)
            {
                unsigned index = 1;
                for (Connection* connection : network->GetClientConnections())
                    result[index++] = connection;
            }
            return result;
        },
        "SetUpdateFps", &Network::SetUpdateFps,
        "RegisterRemoteEvent", [](Network* network, const char* eventName) {
            if (network)
                network->RegisterRemoteEvent(StringHash(eventName));
        },
        "IsServerRunning", &Network::IsServerRunning,
        "StopServer", [](Network* network) {
            if (network)
                network->StopServer();
        },
        // Broadcast a VectorBuffer message to all clients (16_Chat).
        "BroadcastMessage", [](Network* network, int messageId, VectorBuffer* msg) {
            if (network && msg)
                network->BroadcastMessage(static_cast<NetworkMessageId>(messageId), *msg);
        }
    );
    RegisterLuaObjectWrapper<Network>();

    // Connection: a remote peer. Scene replication and messages are managed
    // by the engine; the bindings expose the peer state queries.
    lua.new_usertype<Connection>("Connection",
        sol::no_constructor,
        sol::base_classes, sol::bases<Object>(),
        "Disconnect", static_cast<void (Connection::*)()>(&Connection::Disconnect),
        "GetScene", &Connection::GetScene,
        "SetScene", &Connection::SetScene,
        "IsConnected", &Connection::IsConnected,
        "GetAddress", &Connection::GetAddress,
        // Traffic statistics for the overlay UI (17_SceneReplication).
        "GetPacketsInPerSec", &Connection::GetPacketsInPerSec,
        "GetPacketsOutPerSec", &Connection::GetPacketsOutPerSec,
        "GetBytesInPerSec", &Connection::GetBytesInPerSec,
        "GetBytesOutPerSec", &Connection::GetBytesOutPerSec,
        // Send a VectorBuffer message as reliable + ordered (16_Chat).
        "SendMessage", [](Connection* connection, int messageId, VectorBuffer* msg) {
            if (connection && msg)
                connection->SendMessage(static_cast<NetworkMessageId>(messageId), *msg);
        },
        "ToString", &Connection::ToString
    );
    RegisterLuaObjectWrapper<Connection>();

    // VectorBuffer: construct outgoing network messages (16_Chat). The
    // buffer is not reference-counted (AbstractFile has no RefCounted base),
    // so Lua owns it by value inside the userdata.
    lua.new_usertype<VectorBuffer>("VectorBuffer",
        sol::call_constructor, sol::factories([]() {
            return VectorBuffer();
        }),
        "WriteString", [](VectorBuffer* buffer, const char* text) {
            if (buffer)
                buffer->WriteString(text);
        },
        "ReadString", [](VectorBuffer* buffer) {
            return buffer ? buffer->ReadString() : ea::string();
        },
        "WriteFloat", [](VectorBuffer* buffer, float value) {
            if (buffer)
                buffer->WriteFloat(value);
        },
        "ReadFloat", [](VectorBuffer* buffer) {
            return buffer ? buffer->ReadFloat() : 0.0f;
        },
        "WriteVLE", [](VectorBuffer* buffer, unsigned value) {
            if (buffer)
                buffer->WriteVLE(value);
        },
        "ReadVLE", [](VectorBuffer* buffer) {
            return buffer ? buffer->ReadVLE() : 0u;
        },
        // Rewind before re-reading an in-memory scene snapshot
        // (49_Urho2DIsometricDemo reload).
        "Seek", [](VectorBuffer* buffer, unsigned position) {
            return buffer ? buffer->Seek(position) : 0u;
        },
        "GetSize", [](VectorBuffer* buffer) {
            return buffer ? buffer->GetSize() : 0u;
        },
        "Clear", [](VectorBuffer* buffer) {
            if (buffer)
                buffer->Clear();
        }
    );

    // MemoryBuffer: stream-read incoming network message data. Constructed
    // from a (possibly binary) Lua string delivered in NetworkMessage event
    // data. Registered under the engine type name; the userdata actually
    // holds LuaMemoryBuffer, which owns the bytes the reader points to.
    lua.new_usertype<LuaMemoryBuffer>("MemoryBuffer",
        sol::call_constructor, sol::factories([](std::string data) {
            return LuaMemoryBuffer(data);
        }),
        "ReadString", [](LuaMemoryBuffer* buffer) {
            return buffer ? buffer->buffer_.ReadString() : ea::string();
        },
        "ReadFloat", [](LuaMemoryBuffer* buffer) {
            return buffer ? buffer->buffer_.ReadFloat() : 0.0f;
        },
        "ReadVLE", [](LuaMemoryBuffer* buffer) {
            return buffer ? buffer->buffer_.ReadVLE() : 0u;
        },
        // Read a packed Vector3, used to walk physics contact data
        // (18_CharacterDemo).
        "ReadVector3", [](LuaMemoryBuffer* buffer) {
            return buffer ? buffer->buffer_.ReadVector3() : Vector3::ZERO;
        },
        "IsEof", [](LuaMemoryBuffer* buffer) {
            return buffer ? buffer->buffer_.IsEof() : true;
        }
    );

    // Reserved base for user-defined network message IDs (Protocol.h).
    sol::table msg = lua.create_named_table("MSG");
    msg["USER"] = MSG_USER;

    // ReplicationManager: server-side replication hub component. Created
    // through Scene:CreateComponent("ReplicationManager") (17_SceneReplication).
    lua.new_usertype<ReplicationManager>("ReplicationManager",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "GetClientReplica", &ReplicationManager::GetClientReplica
    );
    RegisterLuaObjectWrapper<ReplicationManager>();

    // ClientReplica: client-side replication session, owned by the manager.
    lua.new_usertype<ClientReplica>("ClientReplica",
        sol::no_constructor,
        "GetOwnedNetworkObject", &ClientReplica::GetOwnedNetworkObject
    );

    // NetworkObject: replicated object component base.
    lua.new_usertype<NetworkObject>("NetworkObject",
        sol::no_constructor,
        sol::base_classes, sol::bases<Component, Serializable, Object>(),
        "GetNode", &Component::GetNode
    );

    // BehaviorNetworkObject: generic replicated object. Node transform and
    // component attributes replicate automatically; custom logic (like
    // player controls) travels through user network messages.
    lua.new_usertype<BehaviorNetworkObject>("BehaviorNetworkObject",
        sol::no_constructor,
        sol::base_classes, sol::bases<NetworkObject, Component, Serializable, Object>(),
        "SetClientPrefab", &StaticNetworkObject::SetClientPrefab,
        "SetOwner", [](BehaviorNetworkObject* object, Connection* owner) {
            if (object)
                object->SetOwner(owner);
        }
    );
    RegisterLuaObjectWrapper<BehaviorNetworkObject>();

    // HttpRequest: async HTTP GET/POST with streamed response
    // (43_HttpRequestDemo). Created through Lua's HttpRequest(url) factory.
    lua.new_usertype<HttpRequest>("HttpRequest",
        sol::call_constructor, sol::factories(
            [](const char* url, sol::optional<std::string> verb, sol::optional<sol::table> headers) {
                ea::vector<ea::string> headerList;
                if (headers)
                {
                    for (int i = 1; i <= static_cast<int>(headers->size()); ++i)
                    {
                        const sol::object header = (*headers)[i];
                        if (header.is<std::string>())
                            headerList.push_back(header.as<std::string>().c_str());
                    }
                }
                return SharedPtr<HttpRequest>(new HttpRequest(url,
                    verb ? ea::string(verb->c_str()) : ea::string{}, headerList));
            }),
        "GetError", &HttpRequest::GetError,
        "GetState", [](HttpRequest* request) {
            return request ? static_cast<int>(request->GetState()) : 0;
        },
        "GetStatusCode", &HttpRequest::GetStatusCode,
        "ReadString", [](HttpRequest* request) {
            return request ? request->ReadString() : ea::string{};
        },
        "IsEof", [](HttpRequest* request) {
            return request ? request->IsEof() : true;
        }
    );
    // HttpRequest derives from RefCounted/Deserializer/Thread only (not Object),
    // so no subsystem caster registration is possible or needed.

    // HTTP connection state constants.
    sol::table httpState = lua.create_named_table("HTTP");
    httpState["INITIALIZING"] = HTTP_INITIALIZING;
    httpState["ERROR"] = HTTP_ERROR;
    httpState["OPEN"] = HTTP_OPEN;
    httpState["CLOSED"] = HTTP_CLOSED;

    // JSONFile: parsed JSON document (43_HttpRequestDemo, 40_Localization).
    lua.new_usertype<JSONFile>("JSONFile",
        sol::call_constructor, sol::factories(
            [context]() { return SharedPtr<JSONFile>(new JSONFile(context)); }),
        sol::base_classes, sol::bases<Resource, Object>(),
        "FromString", [](JSONFile* file, const char* json) {
            return file && file->FromString(json);
        },
        "GetRoot", [](JSONFile* file) -> JSONValue {
            return file ? JSONValue(file->GetRoot()) : JSONValue{};
        }
    );
    RegisterLuaObjectWrapper<JSONFile>();

    // JSONValue: node of a parsed JSON document.
    lua.new_usertype<JSONValue>("JSONValue",
        sol::call_constructor, sol::factories([]() { return JSONValue{}; }),
        "Get", sol::overload(
            [](const JSONValue* value, const char* key) -> JSONValue {
                return value ? JSONValue(value->Get(key)) : JSONValue{};
            },
            [](const JSONValue* value, int index) -> JSONValue {
                return value ? JSONValue(value->Get(index)) : JSONValue{};
            }),
        "GetString", [](const JSONValue* value) {
            return value ? value->GetString() : ea::string{};
        },
        "GetInt", &JSONValue::GetInt,
        "GetBool", &JSONValue::GetBool,
        "GetDouble", &JSONValue::GetDouble,
        "GetFloat", &JSONValue::GetFloat,
        "IsNull", &JSONValue::IsNull,
        "IsObject", &JSONValue::IsObject,
        "IsArray", &JSONValue::IsArray,
        "Size", &JSONValue::Size
    );

    // LANDiscoveryManager: LAN server discovery beacons (53_LANDiscovery).
    // Constructed explicitly, mirroring the C++ sample's MakeShared call.
    lua.new_usertype<LANDiscoveryManager>("LANDiscoveryManager",
        sol::call_constructor, sol::factories([context]() {
            return SharedPtr<LANDiscoveryManager>(new LANDiscoveryManager(context));
        }),
        sol::base_classes, sol::bases<Object>(),
        "Start", [](LANDiscoveryManager* manager, unsigned short port) {
            return manager && manager->Start(port);
        },
        "Stop", &LANDiscoveryManager::Stop,
        "SetBroadcastData", [](LANDiscoveryManager* manager, sol::table data, sol::this_state s) {
            if (!manager)
                return;
            VariantMap map;
            for (const auto& kv : data)
            {
                const sol::object key = kv.first;
                if (!key.is<std::string>())
                    continue;
                map[key.as<std::string>().c_str()] = LuaToVariant(sol::state_view(s), kv.second);
            }
            manager->SetBroadcastData(map);
        },
        "SetBroadcastTimeMs", &LANDiscoveryManager::SetBroadcastTimeMs,
        "GetBroadcastTimeMs", &LANDiscoveryManager::GetBroadcastTimeMs
    );
    RegisterLuaObjectWrapper<LANDiscoveryManager>();
}

} // namespace Urho3D
