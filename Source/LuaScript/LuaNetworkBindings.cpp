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
    {
        using RBFX_THIS = Network;
        RBFX_USERTYPE(Network, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_RAW(Connect, [](Network* network, const char* url, Scene* scene) -> bool {
                return network && network->Connect(URL(url), scene);
            })
            RBFX_RAW(StartServer, [](Network* network, unsigned short port) -> bool {
                return network && network->StartServer(URL(port));
            })
            RBFX_RAW(Disconnect, [](Network* network) {
                if (network)
                    network->Disconnect();
            })
            RBFX_M(GetServerConnection)
            RBFX_RAW(GetClientConnections, [](Network* network, sol::this_state s) -> sol::table {
                sol::state_view lua(s);
                sol::table result = lua.create_table();
                if (network)
                {
                    unsigned index = 1;
                    for (Connection* connection : network->GetClientConnections())
                        result[index++] = connection;
                }
                return result;
            })
            RBFX_M(SetUpdateFps)
            RBFX_RAW(RegisterRemoteEvent, [](Network* network, const char* eventName) {
                if (network)
                    network->RegisterRemoteEvent(StringHash(eventName));
            })
            RBFX_M(IsServerRunning)
            RBFX_RAW(StopServer, [](Network* network) {
                if (network)
                    network->StopServer();
            })
            // Broadcast a VectorBuffer message to all clients (16_Chat).
            RBFX_RAW(BroadcastMessage, [](Network* network, int messageId, VectorBuffer* msg) {
                if (network && msg)
                    network->BroadcastMessage(static_cast<NetworkMessageId>(messageId), *msg);
            })
        );
    }
    RegisterLuaObjectWrapper<Network>();

    // Connection: a remote peer. Scene replication and messages are managed
    // by the engine; the bindings expose the peer state queries.
    {
        using RBFX_THIS = Connection;
        RBFX_USERTYPE(Connection, sol::no_constructor
            RBFX_BASES(Object)
            RBFX_RAW(Disconnect, static_cast<void (Connection::*)()>(&Connection::Disconnect))
            RBFX_M(GetScene)
            RBFX_M(SetScene)
            RBFX_M(IsConnected)
            RBFX_M(GetAddress)
            // Traffic statistics for the overlay UI (17_SceneReplication).
            RBFX_M(GetPacketsInPerSec)
            RBFX_M(GetPacketsOutPerSec)
            RBFX_M(GetBytesInPerSec)
            RBFX_M(GetBytesOutPerSec)
            // Send a VectorBuffer message as reliable + ordered (16_Chat).
            RBFX_RAW(SendMessage, [](Connection* connection, int messageId, VectorBuffer* msg) {
                if (connection && msg)
                    connection->SendMessage(static_cast<NetworkMessageId>(messageId), *msg);
            })
            RBFX_M(ToString)
        );
    }
    RegisterLuaObjectWrapper<Connection>();

    // VectorBuffer: construct outgoing network messages (16_Chat). The
    // buffer is not reference-counted (AbstractFile has no RefCounted base),
    // so Lua owns it by value inside the userdata.
    {
        using RBFX_THIS = VectorBuffer;
        RBFX_USERTYPE(VectorBuffer,
            sol::call_constructor, sol::factories([]() {
                return VectorBuffer();
            })
            RBFX_RAW(WriteString, [](VectorBuffer* buffer, const char* text) {
                if (buffer)
                    buffer->WriteString(text);
            })
            RBFX_RAW(ReadString, [](VectorBuffer* buffer) {
                return buffer ? buffer->ReadString() : ea::string();
            })
            RBFX_RAW(WriteFloat, [](VectorBuffer* buffer, float value) {
                if (buffer)
                    buffer->WriteFloat(value);
            })
            RBFX_RAW(ReadFloat, [](VectorBuffer* buffer) {
                return buffer ? buffer->ReadFloat() : 0.0f;
            })
            RBFX_RAW(WriteVLE, [](VectorBuffer* buffer, unsigned value) {
                if (buffer)
                    buffer->WriteVLE(value);
            })
            RBFX_RAW(ReadVLE, [](VectorBuffer* buffer) {
                return buffer ? buffer->ReadVLE() : 0u;
            })
            // Rewind before re-reading an in-memory scene snapshot
            // (49_Urho2DIsometricDemo reload).
            RBFX_RAW(Seek, [](VectorBuffer* buffer, unsigned position) {
                return buffer ? buffer->Seek(position) : 0u;
            })
            RBFX_RAW(GetSize, [](VectorBuffer* buffer) {
                return buffer ? buffer->GetSize() : 0u;
            })
            RBFX_RAW(Clear, [](VectorBuffer* buffer) {
                if (buffer)
                    buffer->Clear();
            })
        );
    }

    // MemoryBuffer: stream-read incoming network message data. Constructed
    // from a (possibly binary) Lua string delivered in NetworkMessage event
    // data. Registered under the engine type name; the userdata actually
    // holds LuaMemoryBuffer, which owns the bytes the reader points to.
    {
        using RBFX_THIS = LuaMemoryBuffer;
        lua.new_usertype<LuaMemoryBuffer>("MemoryBuffer",
            sol::call_constructor, sol::factories([](std::string data) {
                return LuaMemoryBuffer(data);
            })
            RBFX_RAW(ReadString, [](LuaMemoryBuffer* buffer) {
                return buffer ? buffer->buffer_.ReadString() : ea::string();
            })
            RBFX_RAW(ReadFloat, [](LuaMemoryBuffer* buffer) {
                return buffer ? buffer->buffer_.ReadFloat() : 0.0f;
            })
            RBFX_RAW(ReadVLE, [](LuaMemoryBuffer* buffer) {
                return buffer ? buffer->buffer_.ReadVLE() : 0u;
            })
            // Read a packed Vector3, used to walk physics contact data
            // (18_CharacterDemo).
            RBFX_RAW(ReadVector3, [](LuaMemoryBuffer* buffer) {
                return buffer ? buffer->buffer_.ReadVector3() : Vector3::ZERO;
            })
            RBFX_RAW(IsEof, [](LuaMemoryBuffer* buffer) {
                return buffer ? buffer->buffer_.IsEof() : true;
            })
        );
    }

    // Reserved base for user-defined network message IDs (Protocol.h).
    RBFX_ENUM_TABLE(MSG, "USER", MSG_USER);

    // ReplicationManager: server-side replication hub component. Created
    // through Scene:CreateComponent("ReplicationManager") (17_SceneReplication).
    {
        using RBFX_THIS = ReplicationManager;
        RBFX_USERTYPE(ReplicationManager, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_M(GetClientReplica)
        );
    }
    RegisterLuaObjectWrapper<ReplicationManager>();

    // ClientReplica: client-side replication session, owned by the manager.
    {
        using RBFX_THIS = ClientReplica;
        RBFX_USERTYPE(ClientReplica, sol::no_constructor
            RBFX_M(GetOwnedNetworkObject)
        );
    }

    // NetworkObject: replicated object component base.
    {
        using RBFX_THIS = NetworkObject;
        RBFX_USERTYPE(NetworkObject, sol::no_constructor
            RBFX_BASES(Component, Serializable, Object)
            RBFX_RAW(GetNode, &Component::GetNode)
        );
    }

    // BehaviorNetworkObject: generic replicated object. Node transform and
    // component attributes replicate automatically; custom logic (like
    // player controls) travels through user network messages.
    {
        using RBFX_THIS = BehaviorNetworkObject;
        RBFX_USERTYPE(BehaviorNetworkObject, sol::no_constructor
            RBFX_BASES(NetworkObject, Component, Serializable, Object)
            RBFX_RAW(SetClientPrefab, &StaticNetworkObject::SetClientPrefab)
            RBFX_RAW(SetOwner, [](BehaviorNetworkObject* object, Connection* owner) {
                if (object)
                    object->SetOwner(owner);
            })
        );
    }
    RegisterLuaObjectWrapper<BehaviorNetworkObject>();

    // HttpRequest: async HTTP GET/POST with streamed response
    // (43_HttpRequestDemo). Created through Lua's HttpRequest(url) factory.
    {
        using RBFX_THIS = HttpRequest;
        RBFX_USERTYPE(HttpRequest,
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
                })
            RBFX_M(GetError)
            RBFX_RAW(GetState, [](HttpRequest* request) {
                return request ? static_cast<int>(request->GetState()) : 0;
            })
            RBFX_M(GetStatusCode)
            RBFX_RAW(ReadString, [](HttpRequest* request) {
                return request ? request->ReadString() : ea::string{};
            })
            RBFX_RAW(IsEof, [](HttpRequest* request) {
                return request ? request->IsEof() : true;
            })
        );
    }
    // HttpRequest derives from RefCounted/Deserializer/Thread only (not Object),
    // so no subsystem caster registration is possible or needed.

    // HTTP connection state constants.
    RBFX_ENUM_TABLE(HTTP, "INITIALIZING", HTTP_INITIALIZING, "ERROR", HTTP_ERROR,
        "OPEN", HTTP_OPEN, "CLOSED", HTTP_CLOSED);

    // JSONFile: parsed JSON document (43_HttpRequestDemo, 40_Localization).
    {
        using RBFX_THIS = JSONFile;
        RBFX_USERTYPE(JSONFile,
            sol::call_constructor, sol::factories(
                [context]() { return SharedPtr<JSONFile>(new JSONFile(context)); })
            RBFX_BASES(Resource, Object)
            RBFX_RAW(FromString, [](JSONFile* file, const char* json) {
                return file && file->FromString(json);
            })
            RBFX_RAW(GetRoot, [](JSONFile* file) -> JSONValue {
                return file ? JSONValue(file->GetRoot()) : JSONValue{};
            })
        );
    }
    RegisterLuaObjectWrapper<JSONFile>();

    // JSONValue: node of a parsed JSON document.
    {
        using RBFX_THIS = JSONValue;
        RBFX_USERTYPE(JSONValue,
            sol::call_constructor, sol::factories([]() { return JSONValue{}; })
            RBFX_OVERLOAD(Get,
                [](const JSONValue* value, const char* key) -> JSONValue {
                    return value ? JSONValue(value->Get(key)) : JSONValue{};
                },
                [](const JSONValue* value, int index) -> JSONValue {
                    return value ? JSONValue(value->Get(index)) : JSONValue{};
                })
            RBFX_RAW(GetString, [](const JSONValue* value) {
                return value ? value->GetString() : ea::string{};
            })
            RBFX_M(GetInt)
            RBFX_M(GetBool)
            RBFX_M(GetDouble)
            RBFX_M(GetFloat)
            RBFX_M(IsNull)
            RBFX_M(IsObject)
            RBFX_M(IsArray)
            RBFX_M(Size)
        );
    }

    // LANDiscoveryManager: LAN server discovery beacons (53_LANDiscovery).
    // Constructed explicitly, mirroring the C++ sample's MakeShared call.
    {
        using RBFX_THIS = LANDiscoveryManager;
        RBFX_USERTYPE(LANDiscoveryManager,
            sol::call_constructor, sol::factories([context]() {
                return SharedPtr<LANDiscoveryManager>(new LANDiscoveryManager(context));
            })
            RBFX_BASES(Object)
            RBFX_RAW(Start, [](LANDiscoveryManager* manager, unsigned short port) {
                return manager && manager->Start(port);
            })
            RBFX_M(Stop)
            RBFX_RAW(SetBroadcastData, [](LANDiscoveryManager* manager, sol::table data, sol::this_state s) {
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
            })
            RBFX_M(SetBroadcastTimeMs)
            RBFX_M(GetBroadcastTimeMs)
        );
    }
    RegisterLuaObjectWrapper<LANDiscoveryManager>();
}

} // namespace Urho3D
