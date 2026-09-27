// Copyright (c) 2022-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include <Urho3D/Scene/Component.h>

#include <EASTL/functional.h>

namespace Urho3D
{

class TimelinePlayer;

/// Signal receiver component. Listens to signals fired by TimelinePlayer of the same scene
/// and dispatches them to callbacks, scripted overrides and E_TIMELINE_SIGNALRECEIVED event.
class URHO3D_API SignalReceiver : public Component
{
    URHO3D_OBJECT(SignalReceiver, Component);

public:
    /// Callback invoked when a subscribed signal is received.
    using SignalCallback = ea::function<void(const ea::string& signal, const VariantMap& data, float time)>;

    explicit SignalReceiver(Context* context);
    ~SignalReceiver() override;

    static void RegisterObject(Context* context);

    /// @name Subscriptions
    /// @{
    /// Add signal to the list of received signals.
    void AddSignal(const ea::string& signal);
    /// Remove signal from the list of received signals.
    bool RemoveSignal(const ea::string& signal);
    /// Remove all signals from the list of received signals.
    void RemoveAllSignals();
    /// Return number of subscribed signals.
    unsigned GetNumSignals() const { return signals_.size(); }
    /// Return subscribed signals.
    const StringVector& GetSignals() const { return signals_; }
    /// Return whether the receiver listens to the signal.
    bool IsSubscribed(const ea::string& signal) const;
    /// @}

    /// @name Callbacks
    /// @{
    /// Add a callback invoked when given signal is received. Pass "*" to receive all signals.
    /// Callbacks are not serialized.
    void Subscribe(const ea::string& signal, SignalCallback callback);
    /// Remove all callbacks. Serialized signal list is not changed.
    void ClearCallbacks() { callbacks_.clear(); }
    /// @}

    /// Return whether the receiver handles the signal: it is subscribed to it or has matching callbacks.
    bool HandlesSignal(const ea::string& signal) const;

    /// Called when a signal is received. Override in scripted receivers.
    virtual void OnSignalReceived(const ea::string& signal, const VariantMap& data, float time) {}

    /// Deliver signals fired before this receiver became active. Only signals marked as retroactive are delivered.
    void DeliverRetroactiveSignals();

protected:
    /// Subscribe to events.
    void OnNodeSet(Node* previousNode, Node* currentNode) override;
    /// Deliver retroactive signals when the receiver is enabled.
    void OnSetEnabled() override;

private:
    /// Handle E_TIMELINE_SIGNAL event.
    void HandleTimelineSignal(StringHash eventType, VariantMap& eventData);
    /// Dispatch the signal to callbacks, the overridable method and the event.
    void Deliver(const ea::string& signal, const VariantMap& data, float time);

    /// Serialized signal names.
    StringVector signals_;
    /// Runtime callbacks. Not serialized.
    ea::vector<ea::pair<ea::string, SignalCallback>> callbacks_;
};

}
