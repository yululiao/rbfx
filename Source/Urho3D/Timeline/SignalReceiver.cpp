// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include <Urho3D/Precompiled.h>

#include <Urho3D/Core/ObjectCategory.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Timeline/SignalReceiver.h>
#include <Urho3D/Timeline/TimelineEvents.h>
#include <Urho3D/Timeline/TimelinePlayer.h>
#include <Urho3D/Timeline/TimelineResource.h>

namespace Urho3D
{

namespace
{

/// Wildcard signal name for callbacks.
const char* SIGNAL_WILDCARD = "*";

/// Collect all timeline players of the node hierarchy.
void CollectTimelinePlayers(Node* node, ea::vector<TimelinePlayer*>& players)
{
    ea::vector<TimelinePlayer*> localPlayers;
    node->GetComponents(localPlayers);
    players.insert(players.end(), localPlayers.begin(), localPlayers.end());

    for (const SharedPtr<Node>& child : node->GetChildren())
        CollectTimelinePlayers(child, players);
}

}

SignalReceiver::SignalReceiver(Context* context) :
    Component(context)
{
}

SignalReceiver::~SignalReceiver() = default;

void SignalReceiver::RegisterObject(Context* context)
{
    context->AddFactoryReflection<SignalReceiver>(Category_Timeline);

    URHO3D_ATTRIBUTE("Signals", StringVector, signals_, Variant::emptyStringVector, AM_DEFAULT);
}

void SignalReceiver::AddSignal(const ea::string& signal)
{
    if (!signal.empty() && !IsSubscribed(signal))
        signals_.push_back(signal);
}

bool SignalReceiver::RemoveSignal(const ea::string& signal)
{
    const auto iter = ea::find(signals_.begin(), signals_.end(), signal);
    if (iter == signals_.end())
        return false;

    signals_.erase(iter);
    return true;
}

void SignalReceiver::RemoveAllSignals()
{
    signals_.clear();
}

bool SignalReceiver::IsSubscribed(const ea::string& signal) const
{
    return ea::find(signals_.begin(), signals_.end(), signal) != signals_.end();
}

void SignalReceiver::Subscribe(const ea::string& signal, SignalCallback callback)
{
    if (callback)
        callbacks_.emplace_back(signal, ea::move(callback));
}

bool SignalReceiver::HandlesSignal(const ea::string& signal) const
{
    if (IsSubscribed(signal))
        return true;

    for (const auto& callback : callbacks_)
    {
        if (callback.first == signal || callback.first == SIGNAL_WILDCARD)
            return true;
    }
    return false;
}

void SignalReceiver::OnNodeSet(Node* previousNode, Node* currentNode)
{
    if (currentNode)
    {
        SubscribeToEvent(E_TIMELINE_SIGNAL, &SignalReceiver::HandleTimelineSignal);
        DeliverRetroactiveSignals();
    }
    else
        UnsubscribeFromEvent(E_TIMELINE_SIGNAL);
}

void SignalReceiver::OnSetEnabled()
{
    if (IsEnabled())
        DeliverRetroactiveSignals();
}

void SignalReceiver::DeliverRetroactiveSignals()
{
    if (!IsEnabled())
        return;

    Scene* scene = GetScene();
    if (!scene)
        return;

    ea::vector<TimelinePlayer*> players;
    CollectTimelinePlayers(scene, players);

    for (TimelinePlayer* player : players)
    {
        for (const TimelineFiredSignal& fired : player->GetRetroactiveSignals())
        {
            if (HandlesSignal(fired.signal_))
                Deliver(fired.signal_, fired.data_, fired.time_);
        }
    }
}

void SignalReceiver::HandleTimelineSignal(StringHash eventType, VariantMap& eventData)
{
    if (!IsEnabled())
        return;

    // Accept signals of timeline players from the same scene only.
    Node* senderNode = static_cast<Node*>(eventData[TimelineSignal::P_NODE].GetPtr());
    if (!senderNode || senderNode->GetScene() != GetScene())
        return;

    const ea::string signal = eventData[TimelineSignal::P_SIGNAL].GetString();
    if (!HandlesSignal(signal))
        return;

    Deliver(signal, eventData[TimelineSignal::P_DATA].GetVariantMap(), eventData[TimelineSignal::P_TIME].GetFloat());
}

void SignalReceiver::Deliver(const ea::string& signal, const VariantMap& data, float time)
{
    OnSignalReceived(signal, data, time);

    for (const auto& callback : callbacks_)
    {
        if (callback.first == signal || callback.first == SIGNAL_WILDCARD)
            callback.second(signal, data, time);
    }

    VariantMap& receivedData = GetEventDataMap();
    receivedData[TimelineSignalReceived::P_NODE] = GetNode();
    receivedData[TimelineSignalReceived::P_SIGNAL] = signal;
    receivedData[TimelineSignalReceived::P_DATA] = data;
    SendEvent(E_TIMELINE_SIGNALRECEIVED, receivedData);
}

}
