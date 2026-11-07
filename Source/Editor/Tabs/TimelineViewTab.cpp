//
// Copyright (c) 2022-2024 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "TimelineViewTab.h"

#include "../Project/ModifyResourceAction.h"
#include "../Project/Project.h"
#include "../Project/ProjectRequest.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Animatable.h>
#include <Urho3D/Scene/Component.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/DragDropPayload.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Timeline/TimelineActivationTrack.h>
#include <Urho3D/Timeline/TimelineAnimationTrack.h>
#include <Urho3D/Timeline/TimelineAudioTrack.h>
#include <Urho3D/Timeline/TimelineControlTrack.h>
#include <Urho3D/Timeline/TimelineMarkerTrack.h>
#include <Urho3D/Timeline/TimelinePlayer.h>
#include <Urho3D/Timeline/TimelinePropertyTrack.h>
#include <Urho3D/Timeline/TimelineResource.h>
#include <Urho3D/Timeline/TimelineSignalTrack.h>
#include <Urho3D/Timeline/TimelineTrack.h>

#include <EASTL/set.h>

#include <nfd.h>

#include <cfloat>
#include <cstdio>
#include <type_traits>

namespace Urho3D
{

namespace
{

// Layout constants
constexpr float HeaderWidth = 200.0f;
constexpr float RulerHeight = 26.0f;
constexpr float FrameRulerHeight = 20.0f;
constexpr float RowHeight = 26.0f;
constexpr float ThinRowHeight = 20.0f;
constexpr float RowSpacing = 2.0f;
constexpr float ClipRounding = 3.0f;
constexpr float EdgeGrabWidth = 8.0f;
constexpr float KeySize = 7.0f;
constexpr float MinClipDuration = 0.05f;
constexpr float SnapStep = 0.05f;
constexpr float MinPixelsPerSecond = 5.0f;
constexpr float MaxPixelsPerSecond = 2000.0f;
constexpr float MinRulerStepPx = 90.0f;
constexpr float BindingSlotWidth = 76.0f;

// Colors
const ImU32 ColorBackground = IM_COL32(36, 36, 36, 255);
const ImU32 ColorRulerBg = IM_COL32(61, 56, 55, 255);
const ImU32 ColorTick = IM_COL32(120, 120, 120, 255);
const ImU32 ColorTickMinor = IM_COL32(80, 80, 80, 96);
const ImU32 ColorTickLabel = IM_COL32(187, 187, 187, 255);
const ImU32 ColorRowEven = IM_COL32(58, 54, 54, 255);
const ImU32 ColorRowOdd = IM_COL32(65, 61, 61, 255);
const ImU32 ColorRowSelected = IM_COL32(216, 130, 0, 56);
const ImU32 ColorRowHover = IM_COL32(255, 255, 255, 12);
const ImU32 ColorCursor = IM_COL32(42, 42, 255, 200);
const ImU32 ColorKey = IM_COL32(221, 153, 153, 200);
const ImU32 ColorKeySelected = IM_COL32(255, 255, 255, 255);
const ImU32 ColorClipText = IM_COL32(240, 240, 240, 255);
const ImU32 ColorHeaderText = IM_COL32(210, 210, 210, 255);
const ImU32 ColorHeaderTypeText = IM_COL32(140, 140, 140, 255);
const ImU32 ColorDurationShade = IM_COL32(0, 0, 0, 70);
const ImU32 ColorBindingSlotEmpty = IM_COL32(48, 44, 44, 255);
const ImU32 ColorBindingSlotHighlight = IM_COL32(90, 160, 90, 90);
const ImU32 ColorBindingSlotHighlightBorder = IM_COL32(140, 220, 140, 255);
const ImU32 ColorBindingSlotText = IM_COL32(130, 130, 130, 255);

ImU32 WithAlpha(ImU32 color, unsigned alpha)
{
    return (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, alpha);
}

ea::string FormatTime(float time)
{
    if (time >= 100.0f - 0.001f)
        return Format("{:.0f}s", time);
    if (time >= 10.0f - 0.001f)
        return Format("{:.1f}s", time);
    return Format("{:.2f}s", time);
}

float SnapTime(float time, bool snapEnabled)
{
    if (!snapEnabled)
        return time;
    return floorf(time / SnapStep + 0.5f) * SnapStep;
}

// Track type helpers
bool IsMarkerTrack(const TimelineTrack* track)
{
    return dynamic_cast<const TimelineMarkerTrack*>(track) != nullptr;
}

bool IsPropertyTrack(const TimelineTrack* track)
{
    return dynamic_cast<const TimelinePropertyTrack*>(track) != nullptr;
}

TimelineMarkerTrack* AsMarkerTrack(TimelineTrack* track)
{
    return dynamic_cast<TimelineMarkerTrack*>(track);
}

TimelinePropertyTrack* AsPropertyTrack(TimelineTrack* track)
{
    return dynamic_cast<TimelinePropertyTrack*>(track);
}

float GetRowHeight(const TimelineTrack* track)
{
    return IsMarkerTrack(track) ? ThinRowHeight : RowHeight;
}

ImU32 GetTrackColor(const TimelineTrack* track)
{
    if (dynamic_cast<const TimelineAnimationTrack*>(track))
        return IM_COL32(64, 148, 148, 255);
    if (dynamic_cast<const TimelineControlTrack*>(track))
        return IM_COL32(190, 115, 40, 255);
    if (dynamic_cast<const TimelineActivationTrack*>(track))
        return IM_COL32(96, 150, 96, 255);
    if (dynamic_cast<const TimelineAudioTrack*>(track))
        return IM_COL32(72, 110, 158, 255);
    if (dynamic_cast<const TimelineMarkerTrack*>(track))
        return IM_COL32(170, 140, 30, 255);
    if (dynamic_cast<const TimelineSignalTrack*>(track))
        return IM_COL32(158, 72, 72, 255);
    if (dynamic_cast<const TimelinePropertyTrack*>(track))
        return IM_COL32(130, 100, 170, 255);
    return IM_COL32(110, 110, 110, 255); // Group track
}

// Invokes the callback on the clip vector of the track.
// Returns whether the track type has clips. Group and Marker tracks have none.
// All clip track types store their clips in vectors of TimelineClip-derived structs,
// so type-specific access is centralized here.
template <class Func>
bool DispatchClipVector(const TimelineTrack* track, const Func& func)
{
    if (const auto* t = dynamic_cast<const TimelineAnimationTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineControlTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineActivationTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineAudioTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelineSignalTrack*>(track)) { func(t->clips_); return true; }
    if (const auto* t = dynamic_cast<const TimelinePropertyTrack*>(track)) { func(t->clips_); return true; }
    return false;
}

template <class Func>
bool DispatchClipVector(TimelineTrack* track, const Func& func)
{
    if (auto* t = dynamic_cast<TimelineAnimationTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineControlTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineActivationTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineAudioTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelineSignalTrack*>(track)) { func(t->clips_); return true; }
    if (auto* t = dynamic_cast<TimelinePropertyTrack*>(track)) { func(t->clips_); return true; }
    return false;
}

// Generic clip access
unsigned GetNumClips(const TimelineTrack* track)
{
    unsigned numClips = 0;
    DispatchClipVector(track, [&numClips](const auto& clips) { numClips = clips.size(); });
    return numClips;
}

TimelineClip* GetClip(TimelineTrack* track, unsigned index)
{
    TimelineClip* clip = nullptr;
    DispatchClipVector(track, [index, &clip](auto& clips)
    {
        if (index < clips.size())
            clip = &clips[index];
    });
    return clip;
}

ea::string GetClipLabel(TimelineTrack* track, unsigned index)
{
    if (const auto* animationTrack = dynamic_cast<const TimelineAnimationTrack*>(track))
    {
        if (index >= animationTrack->clips_.size())
            return {};
        return GetFileNameAndExtension(animationTrack->clips_[index].animationRef_.name_);
    }
    if (const auto* controlTrack = dynamic_cast<const TimelineControlTrack*>(track))
    {
        if (index >= controlTrack->clips_.size())
            return {};
        const auto& clip = controlTrack->clips_[index];
        if (!clip.prefabRef_.name_.empty())
            return GetFileNameAndExtension(clip.prefabRef_.name_);
        return GetFileNameAndExtension(clip.timelineRef_.name_);
    }
    if (const auto* activationTrack = dynamic_cast<const TimelineActivationTrack*>(track))
    {
        if (index >= activationTrack->clips_.size())
            return {};
        return activationTrack->clips_[index].active_ ? "active" : "inactive";
    }
    if (const auto* audioTrack = dynamic_cast<const TimelineAudioTrack*>(track))
    {
        if (index >= audioTrack->clips_.size())
            return {};
        return GetFileNameAndExtension(audioTrack->clips_[index].soundRef_.name_);
    }
    if (const auto* signalTrack = dynamic_cast<const TimelineSignalTrack*>(track))
    {
        if (index >= signalTrack->clips_.size())
            return {};
        return signalTrack->clips_[index].signal_;
    }
    if (const auto* propertyTrack = dynamic_cast<const TimelinePropertyTrack*>(track))
    {
        if (index >= propertyTrack->clips_.size())
            return {};
        const unsigned numCurves = propertyTrack->clips_[index].curves_.size();
        return numCurves == 1 ? propertyTrack->clips_[index].curves_[0].name_
                              : Format("{} curves", numCurves);
    }
    return {};
}

void AddDefaultClip(TimelineTrack* track, float startTime)
{
    DispatchClipVector(track, [startTime](auto& clips)
    {
        using ClipType = typename std::remove_reference_t<decltype(clips)>::value_type;
        ClipType clip;
        clip.start_ = startTime;
        if constexpr (std::is_same_v<ClipType, TimelineSignalClip>)
        {
            clip.duration_ = 0.2f;
            clip.signal_ = "Signal";
        }
        clips.push_back(clip);
    });
}

// Create an animation clip from a dropped Animation resource. The clip plays the animation at
// its natural speed, so its duration is the animation length.
void AddAnimationClipFromResource(TimelineAnimationTrack& track, Animation* animation, float startTime)
{
    if (!animation)
        return;

    TimelineAnimationClip clip;
    clip.start_ = Max(startTime, 0.0f);
    clip.duration_ = Max(animation->GetLength(), MinClipDuration);
    clip.animation_ = animation;
    clip.animationRef_ = GetResourceRef(animation, Animation::GetTypeStatic());
    track.clips_.push_back(clip);
}

void RemoveClipAt(TimelineTrack* track, unsigned index)
{
    DispatchClipVector(track, [index](auto& clips)
    {
        if (index < clips.size())
            clips.erase(clips.begin() + static_cast<long>(index));
    });
}

void DuplicateClipAt(TimelineTrack* track, unsigned index)
{
    DispatchClipVector(track, [index](auto& clips)
    {
        if (index < clips.size())
        {
            auto copy = clips[index];
            copy.start_ = copy.start_ + copy.duration_;
            clips.push_back(copy);
        }
    });
}

// Copy the clip at srcIndex of the source track to the destination track when both are of the
// same concrete clip type. Returns whether the copy happened.
bool CopyClipToTrackOfType(TimelineTrack* dst, const TimelineTrack* src, unsigned srcIndex)
{
    bool copied = false;
    DispatchClipVector(src, [&](const auto& srcClips)
    {
        using SrcVec = std::remove_const_t<std::remove_reference_t<decltype(srcClips)>>;
        using SrcClip = typename SrcVec::value_type;
        if (srcIndex >= srcClips.size())
            return;
        DispatchClipVector(dst, [&](auto& dstClips)
        {
            using DstVec = std::remove_reference_t<decltype(dstClips)>;
            using DstClip = typename DstVec::value_type;
            if constexpr (std::is_same_v<SrcClip, DstClip>)
            {
                dstClips.push_back(srcClips[srcIndex]);
                copied = true;
            }
        });
    });
    return copied;
}

SharedPtr<TimelineTrack> CreateTrackByTypeName(Context* context, const ea::string& typeName)
{
    // Return the owning SharedPtr: a raw pointer would dangle as soon as the
    // temporary from MakeShared goes out of scope.
    if (typeName == "Animation")
        return MakeShared<TimelineAnimationTrack>(context);
    if (typeName == "Control")
        return MakeShared<TimelineControlTrack>(context);
    if (typeName == "Activation")
        return MakeShared<TimelineActivationTrack>(context);
    if (typeName == "Audio")
        return MakeShared<TimelineAudioTrack>(context);
    if (typeName == "Marker")
        return MakeShared<TimelineMarkerTrack>(context);
    if (typeName == "Signal")
        return MakeShared<TimelineSignalTrack>(context);
    if (typeName == "Property")
        return MakeShared<TimelinePropertyTrack>(context);
    if (typeName == "Group")
        return MakeShared<TimelineGroupTrack>(context);
    return nullptr;
}

// Property key helpers. Keys of all curves of a clip are expected to be aligned in time,
// so they are displayed and edited as a single set of time points.
ea::vector<float> GetPropertyKeyTimes(const TimelinePropertyClip& clip)
{
    ea::set<float> times;
    for (const auto& curve : clip.curves_)
    {
        for (const auto& key : curve.keys_)
            times.insert(key.time_);
    }
    return ea::vector<float>(times.begin(), times.end());
}

void AddPropertyKeyAt(TimelinePropertyClip& clip, float localTime)
{
    for (auto& curve : clip.curves_)
    {
        // Copy the value from the nearest key, or use the default value of the curve
        const TimelinePropertyKey* nearest = nullptr;
        float bestDist = FLT_MAX;
        for (const auto& key : curve.keys_)
        {
            const float dist = Abs(key.time_ - localTime);
            if (dist < bestDist)
            {
                bestDist = dist;
                nearest = &key;
            }
        }

        TimelinePropertyKey newKey;
        newKey.time_ = localTime;
        newKey.value_ = nearest ? nearest->value_ : Variant(curve.valueType_);

        unsigned insertAt = curve.keys_.size();
        for (unsigned i = 0; i < curve.keys_.size(); ++i)
        {
            if (curve.keys_[i].time_ > localTime)
            {
                insertAt = i;
                break;
            }
        }
        curve.keys_.insert(curve.keys_.begin() + static_cast<long>(insertAt), newKey);
    }
}

void MovePropertyKeysAt(TimelinePropertyClip& clip, float fromLocal, float toLocal)
{
    for (auto& curve : clip.curves_)
    {
        bool moved = false;
        for (auto& key : curve.keys_)
        {
            if (key.time_ == fromLocal)
            {
                key.time_ = toLocal;
                moved = true;
            }
        }
        if (moved)
            ea::stable_sort(curve.keys_.begin(), curve.keys_.end(),
                [](const TimelinePropertyKey& lhs, const TimelinePropertyKey& rhs) { return lhs.time_ < rhs.time_; });
    }
}

void RemovePropertyKeysAt(TimelinePropertyClip& clip, float localTime)
{
    for (auto& curve : clip.curves_)
    {
        curve.keys_.erase(ea::remove_if(curve.keys_.begin(), curve.keys_.end(),
            [localTime](const TimelinePropertyKey& key) { return key.time_ == localTime; }), curve.keys_.end());
    }
}

// Returns the index of the clip that is active at the given time, or M_MAX_UNSIGNED
unsigned FindClipAtTime(TimelineTrack* track, float time)
{
    const unsigned numClips = GetNumClips(track);
    for (unsigned i = 0; i < numClips; ++i)
    {
        const TimelineClip* clip = GetClip(track, i);
        if (clip && time >= clip->start_ && time < clip->GetEndTime())
            return i;
    }
    return M_MAX_UNSIGNED;
}

ea::string MakeUniqueMarkerName(const TimelineMarkerTrack& track)
{
    unsigned index = 1;
    while (true)
    {
        const ea::string name = Format("Marker {}", index);
        bool found = false;
        for (const auto& marker : track.GetMarkers())
        {
            if (marker.name_ == name)
            {
                found = true;
                break;
            }
        }
        if (!found)
            return name;
        ++index;
    }
}

void DrawKeyDiamond(ImDrawList* drawList, const ImVec2& center, ImU32 fillColor, ImU32 borderColor)
{
    const ImVec2 p1(center.x, center.y - KeySize * 0.5f);
    const ImVec2 p2(center.x + KeySize * 0.5f, center.y);
    const ImVec2 p3(center.x, center.y + KeySize * 0.5f);
    const ImVec2 p4(center.x - KeySize * 0.5f, center.y);
    drawList->AddQuadFilled(p1, p2, p3, p4, fillColor);
    drawList->AddQuad(p1, p2, p3, p4, borderColor);
}

// Computes the binding path of the dropped object relative to the nearest TimelinePlayer node of the scene.
// Returns empty string if the object is the player node itself,
// or nullopt if the path cannot be computed (e.g. unnamed nodes).
ea::optional<ea::string> ComputeBindingPath(Scene* scene, Object* target, const TimelineResource* currentTimeline)
{
    if (!target)
        return ea::nullopt;

    Node* targetNode = nullptr;
    ea::string componentSuffix;
    if (auto* component = dynamic_cast<Component*>(target))
    {
        targetNode = component->GetNode();
        componentSuffix = component->GetTypeName();
    }
    else if (auto* node = dynamic_cast<Node*>(target))
    {
        targetNode = node;
    }

    if (!targetNode || targetNode->GetName().empty())
    {
        URHO3D_LOGWARNING("Timeline binding requires named nodes");
        return ea::nullopt;
    }

    // Find the base node: the TimelinePlayer playing this timeline, or the first player, or the scene root
    Node* baseNode = scene;
    if (scene)
    {
        const auto& players = scene->GetComponentIndex(TimelinePlayer::GetTypeStatic());
        const TimelinePlayer* bestPlayer = nullptr;
        for (Component* component : players)
        {
            const auto* player = static_cast<const TimelinePlayer*>(component);
            if (player->GetTimeline() == currentTimeline)
            {
                bestPlayer = player;
                break;
            }
            if (!bestPlayer)
                bestPlayer = player;
        }
        if (bestPlayer)
            baseNode = bestPlayer->GetNode();
    }

    if (targetNode == baseNode)
        return ea::string{};

    // Collect path segments from the target up to the base node
    ea::vector<ea::string> segments;
    const Node* current = targetNode;
    while (current && current != baseNode)
    {
        if (current->GetName().empty())
        {
            URHO3D_LOGWARNING("Timeline binding requires named nodes");
            return ea::nullopt;
        }
        segments.push_back(current->GetName());
        current = current->GetParent();
    }

    // The target is not under the player node: fall back to the node name only,
    // it may still be resolved by the recursive child lookup of the runtime
    if (!current)
        segments.clear();

    ea::string path;
    for (auto iter = segments.rbegin(); iter != segments.rend(); ++iter)
    {
        if (!path.empty())
            path += "/";
        path += *iter;
    }
    if (path.empty())
        path = targetNode->GetName();

    if (!componentSuffix.empty())
    {
        if (!path.empty())
            path += "/";
        path += componentSuffix;
    }
    return path;
}

// Variant types supported by property curves
const char* curveTypeNames[] = {"Float", "Double", "Int", "Bool", "Vector2", "Vector3", "Vector4", "Color", "Quaternion"};
const VariantType curveTypes[] = {VAR_FLOAT, VAR_DOUBLE, VAR_INT, VAR_BOOL,
    VAR_VECTOR2, VAR_VECTOR3, VAR_VECTOR4, VAR_COLOR, VAR_QUATERNION};

// Find attribute of the target by exact name, used by preview and recording.
const AttributeInfo* FindAttributeByName(const Animatable& target, const ea::string& name)
{
    const auto* attributes = target.GetAttributes();
    if (!attributes)
        return nullptr;

    for (const AttributeInfo& info : *attributes)
    {
        if (info.name_ == name)
            return &info;
    }
    return nullptr;
}

// Resolve the object bound to the track using the player's binding rules (override, then path).
// A throwaway runtime is enough: the result is used immediately.
Object* ResolveTrackTarget(TimelinePlayer* player, TimelineTrack* track)
{
    if (!player || !track)
        return nullptr;

    ea::unique_ptr<TimelineTrackRuntime> runtime = track->CreateRuntime();
    return player->ResolveTrackBinding(*track, *runtime);
}

// Property clip that is active at the given timeline time. The last active clip wins,
// matching the evaluation order of TimelinePropertyTrack. Only the clip time range is
// considered: recording targets the clip the cursor is placed on, not extrapolated ones.
TimelinePropertyClip* FindActivePropertyClip(TimelinePropertyTrack& track, float time, float& localTime)
{
    TimelinePropertyClip* result = nullptr;
    for (TimelinePropertyClip& clip : track.clips_)
    {
        if (clip.curves_.empty())
            continue;

        if (time >= clip.start_ && time <= clip.GetEndTime())
        {
            result = &clip;
            localTime = time - clip.start_;
        }
    }
    return result;
}

// Insert the key into the sorted key list, or update the value of the existing key.
void SetCurveKeyValue(TimelinePropertyCurve& curve, float localTime, const Variant& value)
{
    for (TimelinePropertyKey& key : curve.keys_)
    {
        if (Abs(key.time_ - localTime) < 0.001f)
        {
            key.value_ = value;
            return;
        }
    }

    TimelinePropertyKey newKey;
    newKey.time_ = localTime;
    newKey.value_ = value;

    unsigned insertAt = 0;
    while (insertAt < curve.keys_.size() && curve.keys_[insertAt].time_ < localTime)
        ++insertAt;
    curve.keys_.insert(curve.keys_.begin() + static_cast<long>(insertAt), newKey);
}

// Whether any property curve can be recorded at the given time: there is an active clip with
// a curve that matches an attribute of the object bound to the track.
bool HasRecordableProperty(TimelinePlayer* player, TimelineResource* timeline, float time)
{
    if (!player || !timeline)
        return false;

    for (unsigned ti = 0; ti < timeline->GetNumTracks(); ++ti)
    {
        TimelinePropertyTrack* track = AsPropertyTrack(timeline->GetTrack(ti));
        if (!track || track->IsMuted())
            continue;

        float localTime = 0.0f;
        const TimelinePropertyClip* clip = FindActivePropertyClip(*track, time, localTime);
        if (!clip)
            continue;

        auto* target = dynamic_cast<Animatable*>(ResolveTrackTarget(player, track));
        if (!target)
            continue;

        for (const TimelinePropertyCurve& curve : clip->curves_)
        {
            const AttributeInfo* attributeInfo = FindAttributeByName(*target, curve.name_);
            if (attributeInfo && (curve.valueType_ == VAR_NONE || attributeInfo->type_ == curve.valueType_))
                return true;
        }
    }
    return false;
}

// The editor does not run scene updates, so animation instances created by animation tracks
// are never committed to the skeleton. Committing with a zero time step applies the pose at
// the evaluated time without advancing it (TimelinePlayer reports a zero scene time step).

// Find an AnimationController on the node or anywhere in its subtree, so binding a
// PrefabReference container still drives the instantiated model's controller.
AnimationController* FindControllerInSubtreeForPreview(Node* node)
{
    if (!node)
        return nullptr;
    if (auto* controller = node->GetComponent<AnimationController>())
        return controller;
    for (const SharedPtr<Node>& child : node->GetChildren())
    {
        if (auto* controller = FindControllerInSubtreeForPreview(child.Get()))
            return controller;
    }
    return nullptr;
}

void CommitAnimationControllers(TimelinePlayer* player, TimelineResource* timeline)
{
    if (!player || !timeline)
        return;

    for (unsigned ti = 0; ti < timeline->GetNumTracks(); ++ti)
    {
        auto* animationTrack = dynamic_cast<TimelineAnimationTrack*>(timeline->GetTrack(ti));
        if (!animationTrack || animationTrack->IsMuted())
            continue;

        Object* bound = ResolveTrackTarget(player, animationTrack);
        AnimationController* controller = dynamic_cast<AnimationController*>(bound);
        if (!controller)
        {
            if (auto* node = dynamic_cast<Node*>(bound))
                controller = FindControllerInSubtreeForPreview(node);
        }

        if (controller)
            controller->Update(0.0f);
    }
}

// Normalize native path separators to '/' (resource names are '/'-delimited).
void NormalizePath(ea::string& s)
{
    for (char& c : s)
    {
        if (c == '\\')
            c = '/';
    }
}

// ASCII-lowercased copy, for case-insensitive path prefix / suffix tests on Windows.
ea::string LowerCopy(const ea::string& s)
{
    ea::string result = s;
    for (char& c : result)
    {
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    }
    return result;
}

// Write a minimal timeline file. The dialog resolves where the bytes go,
// so write through the absolute path and create missing directories.
bool WriteTimelineFile(Context* context, const ea::string& absPath)
{
    auto* fs = context->GetSubsystem<FileSystem>();
    const size_t sep = absPath.find_last_of("/\\");
    if (sep != ea::string::npos)
        fs->CreateDirsRecursive(absPath.substr(0, sep));

    // Serialize an empty timeline through the engine's own XML writer so the
    // on-disk format always matches what SimpleResource::BeginLoad expects
    // (root block name plus the mandatory "tracks" array block).
    TimelineResource timeline{context};
    File file(context, absPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("TimelineViewTab: cannot open '{}' for writing.", absPath.c_str());
        return false;
    }
    if (!timeline.Save(file, InternalResourceFormat::Xml))
    {
        URHO3D_LOGERROR("TimelineViewTab: failed to serialize empty timeline to '{}'.", absPath.c_str());
        return false;
    }
    return fs->FileExists(absPath);
}

} // namespace

void Tabs_TimelineViewTab(Context* context, Project* project)
{
    project->AddTab(MakeShared<TimelineViewTab>(context));
}

TimelineViewTab::TimelineViewTab(Context* context)
    : ResourceEditorTab(context, "Timeline", "d8a3f5c1-4b27-4e69-9a2f-7c1b5e8d3a04",
        EditorTabFlag::NoContentPadding, EditorTabPlacement::DockCenter)
{
}

TimelineViewTab::~TimelineViewTab()
{
    // The preview may have driven scene attributes: put them back on close.
    StopPreview();
}

bool TimelineViewTab::CanOpenResource(const ResourceFileDescriptor& desc)
{
    return desc.HasObjectType<TimelineResource>();
}

void TimelineViewTab::ResetViewState()
{
    StopPreview();
    recording_ = false;
    recordedValues_.clear();

    viewStartTime_ = 0.0f;
    pixelsPerSecond_ = 100.0f;
    cursorTime_ = 0.0f;
    cursorScrubbed_ = false;
    ClearSelection();
    dragKind_ = DragKind::None;
    pendingAction_ = nullptr;
}

void TimelineViewTab::OnResourceLoaded(const ea::string& resourceName)
{
    auto* cache = GetSubsystem<ResourceCache>();
    timeline_ = cache->GetResource<TimelineResource>(resourceName, false);
    ResetViewState();
}

void TimelineViewTab::OnResourceUnloaded(const ea::string& resourceName)
{
    // The unloaded resource may be the one currently displayed: drop the stale
    // pointer right away, CloseResource re-activates another resource afterwards.
    if (timeline_ && timeline_->GetName() == resourceName)
    {
        timeline_.Reset();
        ResetViewState();
    }
}

void TimelineViewTab::OnActiveResourceChanged(const ea::string& oldResourceName, const ea::string& newResourceName)
{
    // Switching the active resource must switch the displayed timeline as well,
    // otherwise the window keeps rendering the previously loaded one.
    timeline_ = newResourceName.empty()
        ? nullptr
        : GetSubsystem<ResourceCache>()->GetResource<TimelineResource>(newResourceName, false);
    ResetViewState();
}

void TimelineViewTab::OnResourceSaved(const ea::string& resourceName)
{
    // Serialize the in-memory timeline back to its file. Without this the tab would never
    // persist edits: BeginModify() doesn't set SaveOnComplete() on its action, so writing is
    // driven solely through this hook (mirrors UIViewTab/TextEditorTab).
    if (!timeline_ || timeline_->GetName() != resourceName)
        return;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* fs = GetSubsystem<FileSystem>();
    ea::string absPath = cache->GetResourceFileName(resourceName);
    if (absPath.empty())
    {
        auto* project = GetProject();
        absPath = project ? project->GetDataPath() + '/' + resourceName : resourceName;
    }

    const size_t sep = absPath.find_last_of("/\\");
    if (sep != ea::string::npos)
        fs->CreateDirsRecursive(absPath.substr(0, sep));

    File file(context_, absPath, FILE_WRITE);
    if (!file.IsOpen())
    {
        URHO3D_LOGERROR("TimelineViewTab: cannot open '{}' for saving.", absPath.c_str());
        return;
    }
    if (!timeline_->Save(file, InternalResourceFormat::Xml))
        URHO3D_LOGERROR("TimelineViewTab: failed to serialize timeline to '{}'.", absPath.c_str());
}

void TimelineViewTab::OnResourceShallowSaved(const ea::string& resourceName)
{
}

void TimelineViewTab::OnProjectRequest(ProjectRequest* request)
{
    // Follow the node selection of the scene so the window can bind to the selected node.
    // Only remember the selection here: the actual switch happens during the next render.
    const auto* inspectRequest = dynamic_cast<const InspectNodeComponentRequest*>(request);
    if (!inspectRequest)
        return;

    Node* requestedNode = nullptr;
    const auto& nodes = inspectRequest->GetNodes();
    if (!nodes.empty())
        requestedNode = nodes[0];
    else
    {
        const auto& components = inspectRequest->GetComponents();
        if (!components.empty() && components[0])
            requestedNode = components[0]->GetNode();
    }

    if (!requestedNode)
        return;

    // The same click that selects a node also starts a drag&drop of that node to a binding slot.
    // Switching the displayed timeline right away would destroy the slot before the payload is
    // delivered, so the switch is deferred until the mouse is released without dragging.
    pendingContextNode_ = requestedNode;
    pendingNodeSync_ = true;
    pendingSyncWaitsForRelease_ = ui::GetIO().MouseDown[0];
}

void TimelineViewTab::SyncWithSelectedNode()
{
    // The subscription is deferred until the first render: the project may not be
    // registered as a subsystem yet when the tab is constructed
    if (!subscribedToProject_)
    {
        subscribedToProject_ = true;
        if (auto* project = GetProject())
            project->OnRequest.Subscribe(this, &TimelineViewTab::OnProjectRequest);
    }

    if (!pendingNodeSync_)
        return;

    // Wait for the release: a node dragged from the hierarchy must not switch the view
    // while it is being carried to a binding slot of the displayed timeline.
    if (pendingSyncWaitsForRelease_ && ui::GetIO().MouseDown[0])
        return;

    pendingNodeSync_ = false;
    const bool selectionWasClick = pendingSyncWaitsForRelease_;
    pendingSyncWaitsForRelease_ = false;

    Node* node = pendingContextNode_.Get();
    pendingContextNode_.Reset();

    ImGuiIO& io = ui::GetIO();
    const float dragThreshold = io.MouseDragThreshold;
    if (selectionWasClick && io.MouseDragMaxDistanceSqr[0] > dragThreshold * dragThreshold)
        return; // the selection was dragged: keep the current view and binding slots intact

    contextNode_ = node;
    if (!node)
        return; // selection cleared or node deleted: keep the current view

    // The selected node is the single source of truth for the editing state:
    // the window shows whatever its TimelinePlayer is bound to
    auto* player = node->GetComponent<TimelinePlayer>();
    TimelineResource* bound = player ? player->GetTimeline() : nullptr;
    const ea::string resourceName = bound ? bound->GetName() : ea::string{};

    if (resourceName.empty())
    {
        // Node without a timeline binding: show the Create button again.
        // Manually opened resources stay visible until the next selection change.
        if (!GetActiveResourceName().empty())
            SetActiveResource("");
        timeline_ = nullptr;
        ResetViewState();
        return;
    }

    if (!IsResourceOpen(resourceName))
        OpenResource(resourceName);
    else if (GetActiveResourceName() != resourceName)
        SetActiveResource(resourceName);
}

void TimelineViewTab::RenderCreateState()
{
    const Node* node = contextNode_.Get();
    if (!node)
    {
        ui::TextWrapped("%s", "Select a node in the Scene to create a Timeline for it,\nor open an existing .timeline resource.");
        return;
    }

    const char* nodeName = !node->GetName().empty() ? node->GetName().c_str() : "(unnamed node)";
    ui::TextWrapped("Selected node: %s", nodeName);

    ui::Spacing();
    if (ui::Button("Create Timeline", ImVec2(200.0f, 0.0f)))
        CreateTimelineForNode();
}

void TimelineViewTab::CreateTimelineForNode()
{
    Node* node = contextNode_.Get();
    auto* project = GetProject();
    if (!node || !project)
        return;

    // Timelines must live under the project's Data folder to be loadable resources
    auto* fs = GetSubsystem<FileSystem>();
    ea::string dataDir = project->GetDataPath();
    NormalizePath(dataDir);
    if (!dataDir.empty() && dataDir.back() != '/')
        dataDir += '/';

    nfdu8filteritem_t filterItem;
    filterItem.name = "Timeline";
    filterItem.spec = "timeline";
    const char* initialDir = !dataDir.empty() && fs->DirExists(dataDir) ? dataDir.c_str() : nullptr;

    nfdu8char_t* outPath = nullptr;
    const nfdresult_t res = NFD_SaveDialogU8(&outPath, &filterItem, 1, initialDir, "NewTimeline.timeline");
    if (res == NFD_ERROR)
    {
        URHO3D_LOGERROR("TimelineViewTab: save dialog failed: {}", NFD_GetError());
        return;
    }
    if (res != NFD_OKAY)
        return; // canceled

    ea::string chosen = outPath;
    NFD_FreePathU8(outPath);
    NormalizePath(chosen);
    if (chosen.empty())
        return;
    if (chosen.size() < 9 || LowerCopy(chosen).compare(chosen.size() - 9, 9, ".timeline") != 0)
        chosen += ".timeline";

    const ea::string lowerChosen = LowerCopy(chosen);
    const ea::string lowerData = LowerCopy(dataDir);
    if (!dataDir.empty() && lowerChosen.compare(0, lowerData.size(), lowerData) != 0)
    {
        URHO3D_LOGWARNING("TimelineViewTab: timelines must be created under the project Data folder ('{}'); "
            "'{}' was ignored.", dataDir.c_str(), chosen.c_str());
        return;
    }
    const ea::string resourceName = chosen.substr(dataDir.length());
    if (resourceName.empty() || !WriteTimelineFile(context_, chosen))
        return;

    // Load the fresh file, create the player on the node and bind it
    auto* cache = GetSubsystem<ResourceCache>();
    cache->ReleaseResource(resourceName, true);
    auto* timeline = cache->GetResource<TimelineResource>(resourceName);
    if (!timeline)
    {
        URHO3D_LOGERROR("TimelineViewTab: failed to load the created timeline '{}'", resourceName.c_str());
        return;
    }

    auto* player = node->GetComponent<TimelinePlayer>();
    if (!player)
        player = node->CreateComponent<TimelinePlayer>();
    player->SetTimeline(timeline);

    OpenResource(resourceName);
}

TimelinePlayer* TimelineViewTab::GetContextPlayer() const
{
    Node* node = contextNode_.Get();
    if (!node || !timeline_)
        return nullptr;

    // Preview and recording need a player of the displayed timeline: it defines
    // the binding space for the track paths and the objects to drive.
    auto* player = node->GetComponent<TimelinePlayer>();
    if (!player || player->GetTimeline() != timeline_.Get())
        return nullptr;
    return player;
}

void TimelineViewTab::RenderPlaybackControls()
{
    TimelinePlayer* player = GetContextPlayer();
    const float duration = timeline_->GetDuration();

    // Playback controls drive the scene through the player, so they need a bound player.
    // While recording the scene is driven by the Inspector instead.
    ui::BeginDisabled(!player || recording_);

    const bool playing = previewState_ == PreviewState::Playing;
    if (playing)
        ui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.20f, 1.0f));
    if (ui::Button("Play"))
    {
        if (cursorTime_ >= duration - M_EPSILON)
            cursorTime_ = 0.0f;
        if (TimelinePlayer* activePlayer = EnsurePreviewPlayer())
        {
            activePlayer->SetPaused(false);
            previewState_ = PreviewState::Playing;
            previewLastTime_ = ui::GetTime();
            activePlayer->SetTime(cursorTime_);
        }
    }
    if (playing)
        ui::PopStyleColor();

    ui::SameLine();
    ui::BeginDisabled(previewState_ != PreviewState::Playing);
    if (ui::Button("Pause"))
    {
        if (TimelinePlayer* activePlayer = previewPlayer_.Get())
            activePlayer->SetPaused(true);
        previewState_ = PreviewState::Paused;
    }
    ui::EndDisabled();

    ui::SameLine();
    if (ui::Button("Stop"))
    {
        StopPreview();
        cursorTime_ = 0.0f;
    }

    ui::EndDisabled();

    ui::SameLine();
    ui::BeginDisabled(!player && !recording_);
    if (recording_)
        ui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
    const bool toggleRecording = ui::Button(recording_ ? "Stop Rec" : "Record");
    if (recording_)
        ui::PopStyleColor();
    ui::EndDisabled();

    if (toggleRecording)
    {
        if (recording_)
        {
            recording_ = false;
            recordedValues_.clear();
        }
        else
        {
            // Recording samples the values edited in the Inspector: the preview must
            // stop driving them, otherwise every frame would be recorded instead.
            StopPreview();
            recording_ = true;
            recordedValues_.clear();
        }
    }

    if (recording_)
    {
        ui::SameLine();
        ui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "REC");

        // Warn when nothing can be recorded at the cursor: Inspector changes would be lost.
        if (!HasRecordableProperty(player, timeline_, cursorTime_))
        {
            ui::SameLine();
            ui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f), "nothing to record at the cursor: place it over a property clip");
        }
    }
}

TimelinePlayer* TimelineViewTab::EnsurePreviewPlayer()
{
    if (previewState_ != PreviewState::Off)
        return previewPlayer_.Get();

    TimelinePlayer* player = GetContextPlayer();
    if (!player)
        return nullptr;

    previewPlayer_ = player;
    // Enter a playback session and freeze it right away. The tab owns the clock, and the
    // paused session still evaluates when the time is set, so scrubbing works as well.
    player->Play();
    player->SetPaused(true);
    previewState_ = PreviewState::Paused;
    return player;
}

void TimelineViewTab::UpdatePreviewAndRecording()
{
    TimelinePlayer* player = GetContextPlayer();

    // The player or the displayed timeline can change while the preview is running.
    if (previewState_ != PreviewState::Off)
    {
        TimelinePlayer* activePlayer = previewPlayer_.Get();
        if (!activePlayer || activePlayer != player)
            StopPreview();
    }

    // The editor does not run scene updates, so the tab advances the play head itself
    // instead of relying on TimelinePlayer::Update.
    if (previewState_ == PreviewState::Playing)
    {
        if (TimelinePlayer* activePlayer = previewPlayer_.Get())
        {
            const double now = ui::GetTime();
            const float timeStep = Clamp(static_cast<float>(now - previewLastTime_), 0.0f, 0.1f);
            cursorTime_ = Min(cursorTime_ + timeStep, timeline_->GetDuration());
            if (cursorTime_ >= timeline_->GetDuration())
            {
                cursorTime_ = timeline_->GetDuration();
                activePlayer->SetPaused(true);
                previewState_ = PreviewState::Paused;
            }
            activePlayer->SetTime(cursorTime_);
            CommitAnimationControllers(activePlayer, timeline_);
        }
        previewLastTime_ = ui::GetTime();
    }
    else if (cursorScrubbed_ && !recording_)
    {
        // Scrubbing previews the timeline even if playback was never started.
        if (TimelinePlayer* activePlayer = EnsurePreviewPlayer())
        {
            activePlayer->SetTime(cursorTime_);
            CommitAnimationControllers(activePlayer, timeline_);
            previewLastTime_ = ui::GetTime();
        }
    }
    cursorScrubbed_ = false;

    if (recording_ && player)
        UpdateRecording(player);
}

void TimelineViewTab::StopPreview()
{
    if (TimelinePlayer* player = previewPlayer_.Get())
    {
        // Stop finalizes the playback session: tracks release their instances and restore
        // the attributes they drove, so no preview state is left in the scene.
        player->Stop();
    }

    previewPlayer_.Reset();
    previewState_ = PreviewState::Off;
    previewLastTime_ = 0.0;
}

void TimelineViewTab::UpdateRecording(TimelinePlayer* player)
{
    if (!player || !timeline_)
        return;

    bool modificationStarted = false;
    for (unsigned ti = 0; ti < timeline_->GetNumTracks(); ++ti)
    {
        TimelinePropertyTrack* track = AsPropertyTrack(timeline_->GetTrack(ti));
        if (!track || track->IsMuted())
            continue;

        auto* target = dynamic_cast<Animatable*>(ResolveTrackTarget(player, track));
        if (!target)
            continue;

        // Keys are written only where the cursor is covered by a clip of the track.
        float localTime = 0.0f;
        TimelinePropertyClip* clip = FindActivePropertyClip(*track, cursorTime_, localTime);
        if (!clip)
            continue;

        for (TimelinePropertyCurve& curve : clip->curves_)
        {
            const AttributeInfo* attributeInfo = FindAttributeByName(*target, curve.name_);
            if (!attributeInfo || (curve.valueType_ != VAR_NONE && attributeInfo->type_ != curve.valueType_))
                continue;

            Variant value;
            target->OnGetAttribute(*attributeInfo, value);

            const ea::string cacheKey = Format("{}:{}", static_cast<const void*>(target), curve.name_);
            const auto iter = recordedValues_.find(cacheKey);
            if (iter != recordedValues_.end() && iter->second == value)
                continue;

            // The first sample only establishes the baseline, it does not create keys.
            const bool hasBaseline = iter != recordedValues_.end();
            recordedValues_[cacheKey] = value;
            if (!hasBaseline)
                continue;

            if (!modificationStarted)
            {
                BeginModify();
                modificationStarted = true;
            }
            SetCurveKeyValue(curve, localTime, value);
        }
    }
}

float TimelineViewTab::TimeToX(float time, float trackX0) const
{
    return trackX0 + (time - viewStartTime_) * pixelsPerSecond_;
}

float TimelineViewTab::XToTime(float x, float trackX0) const
{
    return viewStartTime_ + (x - trackX0) / pixelsPerSecond_;
}

void TimelineViewTab::BeginModify()
{
    if (!timeline_)
        return;

    // Reuse the action that is still open, e.g. during a drag
    if (pendingAction_ && !pendingAction_->IsComplete())
        return;

    pendingAction_ = MakeShared<ModifyResourceAction>(GetProject());
    pendingAction_->AddResource(timeline_);
    PushAction(pendingAction_);
}

void TimelineViewTab::ClearSelection()
{
    selectedTrack_ = M_MAX_UNSIGNED;
    selectedClip_ = M_MAX_UNSIGNED;
    selectedKey_ = M_MAX_UNSIGNED;
    selectedKeyTime_ = -1.0f;
    multiSelectedClips_.clear();
}

void TimelineViewTab::SelectTrack(unsigned trackIndex)
{
    selectedTrack_ = trackIndex;
    selectedClip_ = M_MAX_UNSIGNED;
    selectedKey_ = M_MAX_UNSIGNED;
    selectedKeyTime_ = -1.0f;
    multiSelectedClips_.clear();
}

TimelineClip* TimelineViewTab::FindSelectedClip(unsigned trackIndex, unsigned clipIndex)
{
    if (!timeline_ || trackIndex >= timeline_->GetTracks().size())
        return nullptr;
    return GetClip(timeline_->GetTracks()[trackIndex].Get(), clipIndex);
}

void TimelineViewTab::CopySelectedClips()
{
    if (!timeline_ || selectedTrack_ >= timeline_->GetTracks().size())
        return;

    // Collect the source clip indices: all multi-selected clips on the current track, otherwise
    // just the single selected clip.
    ea::vector<unsigned> indices;
    for (const auto& [track, clip] : multiSelectedClips_)
    {
        if (track == selectedTrack_)
            indices.push_back(clip);
    }
    if (indices.empty() && selectedClip_ != M_MAX_UNSIGNED)
        indices.push_back(selectedClip_);
    if (indices.empty())
        return;

    TimelineTrack* src = timeline_->GetTracks()[selectedTrack_].Get();
    clipboardTrackType_ = src->GetTrackTypeName();
    clipboardTrack_ = CreateTrackByTypeName(context_, clipboardTrackType_);
    if (!clipboardTrack_)
        return;
    for (unsigned index : indices)
        CopyClipToTrackOfType(clipboardTrack_.Get(), src, index);
}

void TimelineViewTab::PasteClips(float pasteTime)
{
    if (!clipboardTrack_ || !timeline_ || selectedTrack_ >= timeline_->GetTracks().size())
        return;

    TimelineTrack* dst = timeline_->GetTracks()[selectedTrack_].Get();
    if (dst->GetTrackTypeName() != clipboardTrackType_)
        return; // Paste only targets tracks of the copied type

    BeginModify();
    // Shift the pasted clips so the earliest starts at the paste position.
    float minStart = FLT_MAX;
    DispatchClipVector(clipboardTrack_.Get(), [&minStart](const auto& clips)
    {
        for (const auto& clip : clips)
            minStart = Min(minStart, clip.start_);
    });
    if (minStart == FLT_MAX)
        minStart = 0.0f;

    ea::vector<unsigned> pasted;
    const unsigned numPasted = GetNumClips(clipboardTrack_.Get());
    for (unsigned ci = 0; ci < numPasted; ++ci)
    {
        if (CopyClipToTrackOfType(dst, clipboardTrack_.Get(), ci))
        {
            if (TimelineClip* clip = GetClip(dst, GetNumClips(dst) - 1))
            {
                clip->start_ = Max(clip->start_ - minStart + pasteTime, 0.0f);
                pasted.push_back(GetNumClips(dst) - 1);
            }
        }
    }
    if (pasted.empty())
        return;

    // Select the pasted clips.
    selectedClip_ = pasted.front();
    selectedKeyTime_ = -1.0f;
    multiSelectedClips_.clear();
    for (unsigned index : pasted)
        multiSelectedClips_.insert({selectedTrack_, index});
}

void TimelineViewTab::ValidateSelection()
{
    if (!timeline_)
    {
        ClearSelection();
        return;
    }

    const auto& tracks = timeline_->GetTracks();
    if (selectedTrack_ >= tracks.size())
    {
        ClearSelection();
        return;
    }

    TimelineTrack* track = tracks[selectedTrack_].Get();
    if (selectedClip_ >= GetNumClips(track))
    {
        selectedClip_ = M_MAX_UNSIGNED;
        selectedKey_ = M_MAX_UNSIGNED;
        selectedKeyTime_ = -1.0f;
    }

    if (const auto* markerTrack = dynamic_cast<const TimelineMarkerTrack*>(track))
    {
        if (selectedKey_ >= markerTrack->GetMarkers().size())
            selectedKey_ = M_MAX_UNSIGNED;
    }

    // Drop multi-selected clips whose track or clip index became invalid after undo/redo.
    for (auto it = multiSelectedClips_.begin(); it != multiSelectedClips_.end();)
    {
        const bool valid = it->first < tracks.size()
            && it->second < GetNumClips(tracks[it->first].Get());
        it = valid ? ea::next(it) : multiSelectedClips_.erase(it);
    }
    if (multiSelectedClips_.size() <= 1)
        multiSelectedClips_.clear();
}

void TimelineViewTab::RenderContent()
{
    SyncWithSelectedNode();

    if (!timeline_)
    {
        RenderCreateState();
        return;
    }

    ValidateSelection();
    UpdatePreviewAndRecording();

    // The selection panel is taller when a property clip is selected (curve list)
    bool propertyClipSelected = false;
    if (selectedTrack_ < timeline_->GetNumTracks())
    {
        const auto* propertyTrack = AsPropertyTrack(timeline_->GetTrack(selectedTrack_));
        propertyClipSelected = propertyTrack && selectedClip_ < propertyTrack->clips_.size();
    }
    const float panelHeight = propertyClipSelected ? 150.0f : 78.0f;

    RenderToolbar();

    const ImVec2 avail = ui::GetContentRegionAvail();
    const float sequencerHeight = Max(avail.y - panelHeight - 4.0f, 120.0f);
    ui::BeginChild("timeline_sequencer", ImVec2(avail.x, sequencerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    RenderSequencer();
    ui::EndChild();

    RenderSelectionPanel();
}

void TimelineViewTab::RenderToolbar()
{
    // Toolbar may be rendered by the project before any resource is loaded
    if (!timeline_)
        return;

    RenderPlaybackControls();
    ui::SameLine();
    if (ui::Button("Add Track"))
        ui::OpenPopup("AddTrackPopup");

    if (ui::BeginPopup("AddTrackPopup"))
    {
        static const char* trackTypeNames[] = {"Animation", "Control", "Activation", "Audio", "Marker", "Signal", "Property", "Group"};
        for (const char* typeName : trackTypeNames)
        {
            if (ui::Selectable(Format("Add {} Track", typeName).c_str()))
            {
                BeginModify();
                if (SharedPtr<TimelineTrack> track = CreateTrackByTypeName(context_, typeName))
                {
                    timeline_->AddTrack(track);
                    SelectTrack(timeline_->GetNumTracks() - 1);
                }
            }
        }
        ui::EndPopup();
    }
    ui::SameLine();

    {
        int durationMode = timeline_->GetDurationMode() == TimelineDurationMode::Fixed ? 0 : 1;
        ui::SetNextItemWidth(90.0f);
        if (ui::Combo("##durationMode", &durationMode, "Fixed\0Content\0"))
        {
            BeginModify();
            timeline_->SetDurationMode(durationMode == 0 ? TimelineDurationMode::Fixed : TimelineDurationMode::Content);
        }
        if (timeline_->GetDurationMode() == TimelineDurationMode::Fixed)
        {
            ui::SameLine();
            ui::SetNextItemWidth(90.0f);
            float duration = timeline_->GetFixedDuration();
            if (ui::DragFloat("##fixedDuration", &duration, 0.05f, 0.1f, 10000.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp))
            {
                BeginModify();
                timeline_->SetFixedDuration(duration);
            }
        }
    }

    ui::SameLine();
    if (ui::Button("+"))
        pixelsPerSecond_ = Min(pixelsPerSecond_ * 1.5f, MaxPixelsPerSecond);
    ui::SameLine();
    if (ui::Button("-"))
        pixelsPerSecond_ = Max(pixelsPerSecond_ / 1.5f, MinPixelsPerSecond);
    ui::SameLine();
    if (ui::Button("Fit"))
    {
        const float trackAreaWidth = Max(ui::GetContentRegionAvail().x - HeaderWidth - 20.0f, 100.0f);
        pixelsPerSecond_ = Clamp(trackAreaWidth / Max(timeline_->GetDuration(), 0.1f), MinPixelsPerSecond, MaxPixelsPerSecond);
        viewStartTime_ = 0.0f;
    }

    ui::SameLine();
    ui::TextDisabled("%s", FormatTime(cursorTime_).c_str());
    ui::SameLine();
    ui::TextDisabled("wheel on ruler: zoom | middle-drag: pan | Ctrl: no snap");

    if (!bindingError_.empty() && ui::GetTime() - bindingErrorTime_ < 4.0)
    {
        ui::SameLine();
        ui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", bindingError_.c_str());
    }
}

void TimelineViewTab::RenderSequencer()
{
    const float duration = Max(timeline_->GetDuration(), 0.1f);

    // Choose ruler step so that labeled ticks are reasonably spaced
    static const float tickSteps[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f};
    const int numTickSteps = sizeof(tickSteps) / sizeof(tickSteps[0]);
    int stepIndex = 0;
    while (stepIndex + 1 < numTickSteps && tickSteps[stepIndex] * pixelsPerSecond_ < MinRulerStepPx)
        ++stepIndex;
    const float majorStep = tickSteps[stepIndex];
    const float minorStep = majorStep / 5.0f;

    const ImVec2 rulerPos = ui::GetCursorScreenPos();
    const float trackX0 = rulerPos.x + HeaderWidth;
    const float trackX1 = rulerPos.x + ui::GetContentRegionAvail().x;

    // Follow the play head so that playback stays visible without manual panning.
    // The ruler and the body share the same view offset, so pan before drawing either.
    if (previewState_ == PreviewState::Playing)
    {
        const float rightMargin = 60.0f;
        const float cursorX = TimeToX(cursorTime_, trackX0);
        if (cursorX > trackX1 - rightMargin)
            viewStartTime_ = Max(cursorTime_ - (trackX1 - rightMargin - trackX0) / pixelsPerSecond_, 0.0f);
        else if (cursorX < trackX0)
            viewStartTime_ = Max(cursorTime_ - 10.0f / pixelsPerSecond_, 0.0f);
    }

    RenderFrameRuler(duration, trackX0, trackX1);
    // Push the layout down past the frame band so the time ruler draws beneath it, then render.
    ui::Dummy(ImVec2(0.0f, FrameRulerHeight));
    RenderRuler(duration, majorStep, minorStep, trackX0, trackX1);

    ui::BeginChild("timeline_body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, 0);
    RenderBody(duration, majorStep, minorStep);
    ui::EndChild();
}

void TimelineViewTab::RenderRuler(float duration, float majorStep, float minorStep, float trackX0, float trackX1)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    const ImVec2 rulerPos = ui::GetCursorScreenPos();

    drawList->AddRectFilled(rulerPos, ImVec2(trackX1, rulerPos.y + RulerHeight), ColorRulerBg);

    // Shade the area beyond the timeline duration
    const float durationX = TimeToX(duration, trackX0);
    if (durationX < trackX1)
    {
        drawList->AddRectFilled(ImVec2(durationX, rulerPos.y), ImVec2(trackX1, rulerPos.y + RulerHeight), ColorDurationShade);
        drawList->AddLine(ImVec2(durationX, rulerPos.y), ImVec2(durationX, rulerPos.y + RulerHeight), ColorTick, 2.0f);
    }

    // Ticks and labels
    for (float time = floorf(viewStartTime_ / minorStep) * minorStep;; time += minorStep)
    {
        const float x = TimeToX(time, trackX0);
        if (x > trackX1)
            break;
        if (x < trackX0)
            continue;

        const bool major = fabsf(fmodf(time + 0.5f * minorStep, majorStep)) < minorStep;
        const float tickHeight = major ? 10.0f : 5.0f;
        drawList->AddLine(ImVec2(x, rulerPos.y), ImVec2(x, rulerPos.y + tickHeight), ColorTick);
        if (major)
            drawList->AddText(ImVec2(x + 3.0f, rulerPos.y + RulerHeight * 0.5f - 6.0f), ColorTickLabel, FormatTime(time).c_str());
    }

    // Zoom with the mouse wheel while hovering the ruler
    if (ui::IsWindowHovered() && io.MouseWheel != 0.0f)
    {
        pixelsPerSecond_ = Clamp(pixelsPerSecond_ * (io.MouseWheel > 0.0f ? 1.25f : 0.8f), MinPixelsPerSecond, MaxPixelsPerSecond);
        const float timeUnderMouse = XToTime(io.MousePos.x, trackX0);
        viewStartTime_ = Max(timeUnderMouse - (io.MousePos.x - trackX0) / pixelsPerSecond_, 0.0f);
    }

    // Scrub: drag in the ruler to move the cursor
    const ImRect rulerRect(ImVec2(trackX0, rulerPos.y), ImVec2(trackX1, rulerPos.y + RulerHeight));
    if (rulerRect.Contains(io.MousePos) && ui::IsWindowHovered() && ImGui::IsMouseClicked(0))
        dragKind_ = DragKind::Cursor;
    if (dragKind_ == DragKind::Cursor)
    {
        if (!io.MouseDown[0])
            dragKind_ = DragKind::None;
        else
        {
            const float newCursorTime = Max(XToTime(io.MousePos.x, trackX0), 0.0f);
            if (newCursorTime != cursorTime_)
                cursorScrubbed_ = true;
            cursorTime_ = newCursorTime;
        }
    }

    // Cursor marker in the ruler
    const float cursorX = TimeToX(cursorTime_, trackX0);
    if (cursorX >= trackX0 && cursorX <= trackX1)
    {
        drawList->AddRectFilled(ImVec2(cursorX - 5.0f, rulerPos.y + RulerHeight - 9.0f),
            ImVec2(cursorX + 5.0f, rulerPos.y + RulerHeight), ColorCursor);
        if (dragKind_ == DragKind::Cursor)
            drawList->AddText(ImVec2(cursorX + 8.0f, rulerPos.y + 2.0f), ColorTickLabel, FormatTime(cursorTime_).c_str());
    }
}

void TimelineViewTab::RenderFrameRuler(float duration, float trackX0, float trackX1)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    const ImVec2 pos = ui::GetCursorScreenPos();
    const float y0 = pos.y;
    const float y1 = pos.y + FrameRulerHeight;
    const float textY = y0 + (FrameRulerHeight - ui::GetTextLineHeight()) * 0.5f;

    // Background across the whole band; the header cell carries the "Frames" caption.
    drawList->AddRectFilled(ImVec2(pos.x, y0), ImVec2(trackX1, y1), ColorRulerBg);
    drawList->AddText(ImVec2(pos.x + 10.0f, textY), ColorTickLabel, "Frames");

    // Choose a minor (tick) and major (labeled) frame step so spacing stays readable.
    const float secPerFrame = 1.0f / Max(fps_, 1.0f);
    const float pxPerFrame = pixelsPerSecond_ * secPerFrame;
    static const int frameSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600, 7200};
    const int numFrameSteps = static_cast<int>(sizeof(frameSteps) / sizeof(frameSteps[0]));
    int minorIdx = 0;
    while (minorIdx + 1 < numFrameSteps && frameSteps[minorIdx] * pxPerFrame < 6.0f)
        ++minorIdx;
    int majorIdx = minorIdx;
    while (majorIdx + 1 < numFrameSteps && frameSteps[majorIdx] * pxPerFrame < MinRulerStepPx)
        ++majorIdx;
    const float minorSec = frameSteps[minorIdx] * secPerFrame;
    const int majorFrames = frameSteps[majorIdx];

    // Shade the area beyond the timeline duration.
    const float durationX = TimeToX(duration, trackX0);
    if (durationX < trackX1)
        drawList->AddRectFilled(ImVec2(durationX, y0), ImVec2(trackX1, y1), ColorDurationShade);

    for (float t = floorf(viewStartTime_ / minorSec) * minorSec;; t += minorSec)
    {
        const float x = TimeToX(t, trackX0);
        if (x > trackX1)
            break;
        if (x < trackX0)
            continue;
        const int frame = static_cast<int>(lroundf(t * fps_));
        const bool major = (frame % majorFrames) == 0;
        const float tickHeight = major ? 9.0f : 4.0f;
        drawList->AddLine(ImVec2(x, y1 - tickHeight), ImVec2(x, y1), ColorTick);
        if (major)
        {
            const ea::string label = Format("{}", frame);
            drawList->AddText(ImVec2(x + 3.0f, textY), ColorTickLabel, label.c_str());
        }
    }

    // Cursor line through the frame band.
    const float cursorX = TimeToX(cursorTime_, trackX0);
    if (cursorX >= trackX0 && cursorX <= trackX1)
        drawList->AddLine(ImVec2(cursorX, y0), ImVec2(cursorX, y1), ColorCursor, 2.0f);

    // Click or drag in the band scrubs the cursor. Continuation and release are handled by the
    // time ruler below, which owns the shared DragKind::Cursor state.
    const ImRect bandRect(ImVec2(trackX0, y0), ImVec2(trackX1, y1));
    if (bandRect.Contains(io.MousePos) && ui::IsWindowHovered() && ImGui::IsMouseClicked(0))
    {
        dragKind_ = DragKind::Cursor;
        const float newCursorTime = Max(XToTime(io.MousePos.x, trackX0), 0.0f);
        if (newCursorTime != cursorTime_)
            cursorScrubbed_ = true;
        cursorTime_ = newCursorTime;
    }
}

void TimelineViewTab::RenderBody(float duration, float majorStep, float minorStep)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    const auto& tracks = timeline_->GetTracks();

    const ImVec2 canvasPos = ui::GetCursorScreenPos();
    const ImVec2 windowPos = ui::GetWindowPos();
    const ImVec2 windowSize = ui::GetWindowSize();
    const ImRect visibleRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y));
    const float trackX0 = canvasPos.x + HeaderWidth;
    const float trackX1 = windowPos.x + windowSize.x;

    // Compute row layout
    struct RowLayout
    {
        unsigned trackIndex_{M_MAX_UNSIGNED};
        float y0_{};
        float y1_{};
    };
    ea::vector<RowLayout> rows;
    rows.reserve(tracks.size());
    float contentHeight = 8.0f;
    for (unsigned i = 0; i < tracks.size(); ++i)
    {
        const float height = GetRowHeight(tracks[i].Get());
        rows.push_back(RowLayout{i, contentHeight, contentHeight + height});
        contentHeight += height + RowSpacing;
    }

    // Reserve the scrollable extent
    ui::Dummy(ImVec2(0.0f, contentHeight));
    const float originY = canvasPos.y - ui::GetScrollY();

    const auto rowRectOf = [&](const RowLayout& row)
    {
        return ImRect(ImVec2(canvasPos.x, originY + row.y0_), ImVec2(trackX1, originY + row.y1_));
    };

    RenderBodyGrid(duration, majorStep, minorStep, canvasPos, trackX0, trackX1, visibleRect.Min, visibleRect.Max);

    const bool windowHovered = ui::IsWindowHovered();

    // Track reorder drag: drop the dragged track onto the row under the cursor. UpdateDrag
    // clears the drag on mouse release, so only the in-drag repositioning happens here.
    if (dragKind_ == DragKind::TrackReorder && !rows.empty())
    {
        const float grabY = io.MousePos.y - dragReorderGrab_;
        unsigned target = dragTrack_;
        for (const RowLayout& row : rows)
        {
            const float y0 = originY + row.y0_;
            const float y1 = originY + row.y1_;
            if (grabY >= y0 && grabY < y1)
            {
                target = row.trackIndex_;
                break;
            }
            if (grabY >= y1)
                target = row.trackIndex_;
        }
        if (target != dragTrack_ && target < tracks.size())
        {
            timeline_->MoveTrack(dragTrack_, target);
            dragTrack_ = target;
            selectedTrack_ = target;
        }
    }

    // Rows
    for (const RowLayout& row : rows)
    {
        const ImRect rowRect = rowRectOf(row);
        if (rowRect.Max.y < visibleRect.Min.y || rowRect.Min.y > visibleRect.Max.y)
            continue;

        TimelineTrack* track = tracks[row.trackIndex_].Get();
        const bool rowHovered = windowHovered && rowRect.Contains(io.MousePos);
        const bool rowSelected = row.trackIndex_ == selectedTrack_;

        // Row background
        ImU32 rowColor = (row.trackIndex_ & 1) ? ColorRowOdd : ColorRowEven;
        if (rowSelected)
            rowColor = ColorRowSelected;
        else if (rowHovered)
            rowColor = WithAlpha(rowColor, 255) == rowColor ? (rowColor + ColorRowHover) : rowColor; // additive hover tint
        drawList->AddRectFilled(rowRect.Min, rowRect.Max, rowColor);

        RenderTrackHeader(track, row.trackIndex_, rowRect.Min, rowRect.Max, trackX0, rowSelected, windowHovered);
        RenderTrackContent(track, row.trackIndex_, rowRect.Min, rowRect.Max, trackX0, trackX1, rowSelected);
    }

    // Cursor line across the body
    const float cursorX = TimeToX(cursorTime_, trackX0);
    if (cursorX >= trackX0 && cursorX <= trackX1)
        drawList->AddLine(ImVec2(cursorX, visibleRect.Min.y), ImVec2(cursorX, visibleRect.Max.y), ColorCursor, 2.0f);

    // Interactions
    UpdateDrag(trackX0);

    if (windowHovered && dragKind_ == DragKind::None && visibleRect.Contains(io.MousePos))
    {
        for (const RowLayout& row : rows)
        {
            const ImRect rowRect = rowRectOf(row);
            if (!rowRect.Contains(io.MousePos))
                continue;

            ProcessRowHitTesting(tracks[row.trackIndex_].Get(), row.trackIndex_,
                rowRect.Min, rowRect.Max, trackX0);
            break;
        }
    }

    ProcessDeleteKey();
    ProcessEditKeys();

    RenderTrackAreaContextMenu();
    RenderClipContextMenu();
    RenderTrackHeaderContextMenu();
}

void TimelineViewTab::RenderBodyGrid(float duration, float majorStep, float minorStep, const ImVec2& canvasPos,
    float trackX0, float trackX1, const ImVec2& visibleMin, const ImVec2& visibleMax)
{
    ImDrawList* drawList = ui::GetWindowDrawList();

    // Draw vertical grid lines of the visible area
    for (float time = floorf(viewStartTime_ / minorStep) * minorStep;; time += minorStep)
    {
        const float x = TimeToX(time, trackX0);
        if (x > trackX1)
            break;
        if (x < trackX0)
            continue;
        const bool major = fabsf(fmodf(time + 0.5f * minorStep, majorStep)) < minorStep;
        drawList->AddLine(ImVec2(x, visibleMin.y), ImVec2(x, visibleMax.y), major ? ColorTick : ColorTickMinor, major ? 1.5f : 1.0f);
    }

    // Shade the area beyond the timeline duration
    const float durationX = TimeToX(duration, trackX0);
    if (durationX < trackX1)
        drawList->AddRectFilled(ImVec2(durationX, visibleMin.y), ImVec2(trackX1, visibleMax.y), ColorDurationShade);

    // Background of the header area
    drawList->AddRectFilled(ImVec2(canvasPos.x, visibleMin.y), ImVec2(trackX0, visibleMax.y), ColorBackground);
}

void TimelineViewTab::RenderTrackHeader(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin,
    const ImVec2& rowMax, float trackX0, bool rowSelected, bool windowHovered)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    const ImRect rowRect(rowMin, rowMax);
    const float fontHeight = ui::GetTextLineHeight();
    const float textY = rowMin.y + (rowRect.GetHeight() - fontHeight) * 0.5f;

    // Track type color bar and header text
    drawList->AddRectFilled(rowMin, ImVec2(rowMin.x + 4.0f, rowMax.y), GetTrackColor(track));
    const ea::string typeText = track->GetTrackTypeName();
    drawList->AddText(ui::GetFont(), ui::GetFontSize(), ImVec2(rowMin.x + 10.0f, textY), ColorHeaderTypeText, typeText.c_str());
    const ImVec2 typeTextSize = ui::CalcTextSize(typeText.c_str());

    // Binding slot on the right side of the header: drop a node or a component here to bind the track to it
    const ImRect slotRect(ImVec2(trackX0 - BindingSlotWidth - 6.0f, rowMin.y + 2.0f),
        ImVec2(trackX0 - 6.0f, rowMax.y - 2.0f));
    const ea::string& trackBinding = track->GetBindingPath();
    const bool slotOccupied = !trackBinding.empty();
    bool slotHighlighted = false;

    ui::PushID(static_cast<int>(trackIndex));
    if (ui::BeginDragDropTargetCustom(slotRect, ui::GetID("binding_slot")))
    {
        const auto* nodePayload = dynamic_cast<const NodeComponentDragDropPayload*>(DragDropPayload::Get());
        if (nodePayload && (!nodePayload->nodes_.empty() || !nodePayload->components_.empty()))
        {
            if (ui::AcceptDragDropPayload(DragDropPayloadType.c_str(),
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                slotHighlighted = true;
                if (ui::GetDragDropPayload()->IsDelivery())
                {
                    Object* dropTarget = !nodePayload->components_.empty()
                        ? static_cast<Object*>(nodePayload->components_[0].Get())
                        : static_cast<Object*>(nodePayload->nodes_[0].Get());
                    const auto path = ComputeBindingPath(nodePayload->scene_.Get(), dropTarget, timeline_.Get());
                    if (path)
                    {
                        BeginModify();
                        track->SetBindingPath(*path);
                        bindingError_.clear();
                    }
                    else
                    {
                        bindingError_ = "Binding failed: all nodes from the player to the target must be named";
                        bindingErrorTime_ = ui::GetTime();
                    }
                }
            }
        }
        ui::EndDragDropTarget();
    }
    ui::PopID();

    if (windowHovered && slotRect.Contains(io.MousePos))
        slotHighlighted = true;

    drawList->AddRectFilled(slotRect.Min, slotRect.Max,
        slotHighlighted ? ColorBindingSlotHighlight : (slotOccupied ? WithAlpha(GetTrackColor(track), 70) : ColorBindingSlotEmpty), 3.0f);
    drawList->AddRect(slotRect.Min, slotRect.Max,
        slotHighlighted ? ColorBindingSlotHighlightBorder : WithAlpha(GetTrackColor(track), 140), 3.0f, 0, 1.0f);

    // Slot label: the last segment of the binding path, or a hint when unbound
    const char* slotLabel = "drop";
    if (slotOccupied)
    {
        const auto lastSlash = trackBinding.find_last_of('/');
        slotLabel = lastSlash == ea::string::npos
            ? trackBinding.c_str()
            : trackBinding.c_str() + lastSlash + 1;
    }
    const ImVec2 slotLabelSize = ui::CalcTextSize(slotLabel);
    if (slotLabelSize.x <= slotRect.GetWidth() - 4.0f)
        drawList->AddText(ImVec2(slotRect.Min.x + (slotRect.GetWidth() - slotLabelSize.x) * 0.5f, textY),
            slotOccupied ? ColorHeaderText : ColorBindingSlotText, slotLabel);

    // Track name, clipped to the space left of the binding slot
    const ImVec2 nameTextPos(rowMin.x + 10.0f + typeTextSize.x + 8.0f, textY);
    drawList->PushClipRect(ImVec2(rowMin.x, rowMin.y), ImVec2(slotRect.Min.x - 6.0f, rowMax.y), true);
    drawList->AddText(nameTextPos, track->IsMuted() ? ColorHeaderTypeText : ColorHeaderText, track->GetName().c_str());
    drawList->PopClipRect();

    // Tooltip with full track info
    const ImRect headerRect(rowMin, ImVec2(trackX0, rowMax.y));
    if (windowHovered && headerRect.Contains(io.MousePos))
    {
        ui::SetTooltip("%s track\nName: %s\nBinding: %s\nWeight: %.2f%s",
            typeText.c_str(), track->GetName().c_str(), trackBinding.empty() ? "(unbound)" : trackBinding.c_str(),
            track->GetWeight(), track->IsMuted() ? "\n(muted)" : "");
    }
    if (windowHovered && slotRect.Contains(io.MousePos))
    {
        ui::SetTooltip("Binding: %s\nDrag a node or a component from the Scene hierarchy\nand drop it here to bind the track to it.",
            trackBinding.empty() ? "(unbound)" : trackBinding.c_str());
    }
}

void TimelineViewTab::RenderTrackContent(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin,
    const ImVec2& rowMax, float trackX0, float trackX1, bool rowSelected)
{
    ImDrawList* drawList = ui::GetWindowDrawList();
    const ImRect rowRect(rowMin, rowMax);
    const float fontHeight = ui::GetTextLineHeight();
    const float textY = rowMin.y + (rowRect.GetHeight() - fontHeight) * 0.5f;

    // Clip drawing to the track area
    drawList->PushClipRect(ImVec2(trackX0, rowMin.y), ImVec2(trackX1, rowMax.y), true);

    const ImU32 trackColor = GetTrackColor(track);

    if (const auto* markerTrack = dynamic_cast<const TimelineMarkerTrack*>(track))
    {
        // Markers: diamond + vertical line
        const auto& markers = markerTrack->GetMarkers();
        for (unsigned mi = 0; mi < markers.size(); ++mi)
        {
            const auto& marker = markers[mi];
            const float x = TimeToX(marker.time_, trackX0);
            if (x < trackX0 - KeySize || x > trackX1 + KeySize)
                continue;

            const ImVec2 center(x, (rowMin.y + rowMax.y) * 0.5f);
            const bool markerSelected = rowSelected && mi == selectedKey_;
            drawList->AddLine(ImVec2(x, rowMin.y), ImVec2(x, rowMax.y),
                markerSelected ? ColorKeySelected : WithAlpha(trackColor, 200), 1.5f);
            DrawKeyDiamond(drawList, center,
                markerSelected ? ColorKeySelected : ColorKey, markerSelected ? ColorKeySelected : IM_COL32(0, 0, 0, 160));
            drawList->AddText(ImVec2(x + KeySize, textY), ColorHeaderText, marker.name_.c_str());
        }
    }
    else
    {
        const unsigned numClips = GetNumClips(track);
        for (unsigned ci = 0; ci < numClips; ++ci)
        {
            const TimelineClip* clip = GetClip(track, ci);
            if (!clip)
                continue;

            const float clipX0 = TimeToX(clip->start_, trackX0);
            const float clipX1 = TimeToX(clip->GetEndTime(), trackX0);
            if (clipX1 < trackX0 || clipX0 > trackX1)
                continue;

            const bool inMulti = multiSelectedClips_.count(ea::make_pair(trackIndex, ci)) != 0;
            const bool clipSelected = (rowSelected && ci == selectedClip_) || inMulti;
            const ImVec2 blockMin(clipX0, rowMin.y + 2.0f);
            const ImVec2 blockMax(clipX1, rowMax.y - 2.0f);

            drawList->AddRectFilled(blockMin, blockMax, WithAlpha(trackColor, 76), ClipRounding);
            drawList->AddRect(blockMin, blockMax, clipSelected ? IM_COL32(255, 255, 255, 255) : WithAlpha(trackColor, 255),
                ClipRounding, 0, clipSelected ? 2.0f : 1.0f);

            const ea::string label = GetClipLabel(track, ci);
            if (!label.empty() && blockMax.x - blockMin.x > 30.0f)
            {
                const ImVec2 textSize = ui::CalcTextSize(label.c_str());
                const ImVec2 textPos(Max(blockMin.x + 4.0f, (blockMin.x + blockMax.x - textSize.x) * 0.5f), textY);
                drawList->AddText(textPos, ColorClipText, label.c_str());
            }

            // Trim handles of the selected clip
            if (clipSelected && blockMax.x - blockMin.x > 4.0f * EdgeGrabWidth)
            {
                drawList->AddRectFilled(ImVec2(blockMin.x, blockMin.y + 4.0f), ImVec2(blockMin.x + 3.0f, blockMax.y - 4.0f), IM_COL32(255, 255, 255, 160));
                drawList->AddRectFilled(ImVec2(blockMax.x - 3.0f, blockMin.y + 4.0f), ImVec2(blockMax.x, blockMax.y - 4.0f), IM_COL32(255, 255, 255, 160));
            }

            // Keyframes of property clips: keys of all curves are displayed at their common times
            if (const auto* propertyTrack = dynamic_cast<const TimelinePropertyTrack*>(track))
            {
                if (ci < propertyTrack->clips_.size())
                {
                    const auto& propertyClip = propertyTrack->clips_[ci];
                    const float keyY = (rowMin.y + rowMax.y) * 0.5f;
                    for (float keyTime : GetPropertyKeyTimes(propertyClip))
                    {
                        const float x = TimeToX(clip->start_ + keyTime, trackX0);
                        if (x < trackX0 - KeySize || x > trackX1 + KeySize)
                            continue;
                        const bool keySelected = clipSelected && keyTime == selectedKeyTime_;
                        DrawKeyDiamond(drawList, ImVec2(x, keyY),
                            keySelected ? ColorKeySelected : ColorKey,
                            keySelected ? ColorKeySelected : IM_COL32(0, 0, 0, 160));
                    }
                }
            }
        }
    }

    drawList->PopClipRect();

    // Dropping a resource on the clip area creates a clip of the track
    RenderTrackDropTarget(track, trackIndex, ImVec2(trackX0, rowMin.y), ImVec2(trackX1, rowMax.y), trackX0);
}

void TimelineViewTab::RenderTrackDropTarget(TimelineTrack* track, unsigned trackIndex, const ImVec2& contentMin,
    const ImVec2& contentMax, float trackX0)
{
    auto* animationTrack = dynamic_cast<TimelineAnimationTrack*>(track);
    if (!animationTrack)
        return;

    ImGuiIO& io = ui::GetIO();
    ui::PushID(static_cast<int>(trackIndex));
    if (ui::BeginDragDropTargetCustom(ImRect(contentMin, contentMax), ui::GetID("track_clip_drop")))
    {
        const auto* payload = dynamic_cast<const ResourceDragDropPayload*>(DragDropPayload::Get());
        if (payload && payload->resources_.size() == 1 && payload->resources_[0].HasObjectType<Animation>())
        {
            if (ui::AcceptDragDropPayload(DragDropPayloadType.c_str(),
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                const float startTime = SnapTime(Max(XToTime(io.MousePos.x, trackX0), 0.0f), !io.KeyCtrl);

                // Feedback: the whole row accepts the drop, the line marks where the clip will start
                ImDrawList* drawList = ui::GetWindowDrawList();
                drawList->AddRectFilled(contentMin, contentMax, WithAlpha(GetTrackColor(track), 48));
                const float startX = TimeToX(startTime, trackX0);
                drawList->AddLine(ImVec2(startX, contentMin.y + 2.0f), ImVec2(startX, contentMax.y - 2.0f),
                    ColorBindingSlotHighlightBorder, 2.0f);

                if (ui::GetDragDropPayload()->IsDelivery())
                {
                    if (auto* animation = GetSubsystem<ResourceCache>()
                            ->GetResource<Animation>(payload->resources_[0].resourceName_))
                    {
                        BeginModify();
                        AddAnimationClipFromResource(*animationTrack, animation, startTime);
                    }
                }
            }
        }
        ui::EndDragDropTarget();
    }
    ui::PopID();
}

void TimelineViewTab::ProcessRowHitTesting(TimelineTrack* track, unsigned trackIndex,
    const ImVec2& rowMin, const ImVec2& rowMax, float trackX0)
{
    ImGuiIO& io = ui::GetIO();
    const ImRect rowRect(rowMin, rowMax);
    const bool leftClick = ImGui::IsMouseClicked(0);
    const bool rightClick = ImGui::IsMouseClicked(1);

    // Click on the header selects the track
    if (io.MousePos.x < trackX0)
    {
        if (leftClick)
        {
            SelectTrack(trackIndex);
            // Drag the header vertically to reorder tracks. Only start when grabbing the
            // name area (left of the binding slot), so the slot keeps its drop behaviour.
            const float slotLeft = trackX0 - 46.0f;
            if (io.MousePos.x < slotLeft)
            {
                BeginModify();
                dragKind_ = DragKind::TrackReorder;
                dragTrack_ = trackIndex;
                dragReorderGrab_ = io.MousePos.y - rowMin.y;
            }
        }
        if (rightClick)
        {
            popupTrack_ = trackIndex;
            ui::OpenPopup("TrackHeaderContextMenu");
        }
        return;
    }

    const float mouseTime = XToTime(io.MousePos.x, trackX0);

    if (auto* markerTrack = AsMarkerTrack(track))
    {
        if (!ProcessMarkerHitTesting(markerTrack, trackIndex, rowMin, rowMax, trackX0, leftClick, rightClick))
        {
            if (leftClick)
                SelectTrack(trackIndex);
            if (rightClick)
            {
                popupTrack_ = trackIndex;
                popupClip_ = M_MAX_UNSIGNED;
                popupTime_ = mouseTime;
                ui::OpenPopup("TrackAreaContextMenu");
            }
        }
        return;
    }

    if (auto* propertyTrack = AsPropertyTrack(track))
    {
        // Keys are hit-tested first, they are drawn on top of the clips
        if (ProcessPropertyKeyHitTesting(propertyTrack, trackIndex, rowMin, rowMax, trackX0, leftClick))
            return;
    }

    if (ProcessClipHitTesting(track, trackIndex, rowMin, rowMax, trackX0, leftClick, rightClick))
        return;

    // Click on the empty track area selects the track
    if (leftClick)
        SelectTrack(trackIndex);
    if (rightClick)
    {
        popupTrack_ = trackIndex;
        popupClip_ = M_MAX_UNSIGNED;
        popupTime_ = mouseTime;
        ui::OpenPopup("TrackAreaContextMenu");
    }
}

bool TimelineViewTab::ProcessClipHitTesting(TimelineTrack* track, unsigned trackIndex, const ImVec2& rowMin,
    const ImVec2& rowMax, float trackX0, bool leftClick, bool rightClick)
{
    ImGuiIO& io = ui::GetIO();
    const unsigned numClips = GetNumClips(track);
    for (unsigned ci = 0; ci < numClips; ++ci)
    {
        TimelineClip* clip = GetClip(track, ci);
        if (!clip)
            continue;

        const float clipX0 = TimeToX(clip->start_, trackX0);
        const float clipX1 = TimeToX(clip->GetEndTime(), trackX0);
        const bool inRowY = io.MousePos.y >= rowMin.y && io.MousePos.y <= rowMax.y;
        const bool inClipX = io.MousePos.x >= clipX0 - EdgeGrabWidth && io.MousePos.x <= clipX1 + EdgeGrabWidth;
        if (!inRowY || !inClipX)
            continue;

        if (rightClick)
        {
            popupTrack_ = trackIndex;
            popupClip_ = ci;
            popupTime_ = XToTime(io.MousePos.x, trackX0);
            ui::OpenPopup("ClipContextMenu");
            return true;
        }

        if (leftClick)
        {
            // Ctrl+click toggles the clip in the multi-selection without starting a drag.
            if (io.KeyCtrl)
            {
                const auto pair = ea::make_pair(trackIndex, ci);
                if (!multiSelectedClips_.erase(pair))
                    multiSelectedClips_.insert(pair);
                selectedTrack_ = trackIndex;
                selectedClip_ = ci;
                selectedKey_ = M_MAX_UNSIGNED;
                selectedKeyTime_ = -1.0f;
                return true;
            }

            // A clip inside an existing multi-selection keeps the group and drags it as a batch;
            // otherwise the selection collapses to this single clip.
            if (multiSelectedClips_.size() > 1 && multiSelectedClips_.count(ea::make_pair(trackIndex, ci)))
            {
                selectedTrack_ = trackIndex;
                selectedClip_ = ci;
            }
            else
            {
                SelectTrack(trackIndex);
                selectedClip_ = ci;
                multiSelectedClips_.clear();
                multiSelectedClips_.insert(ea::make_pair(trackIndex, ci));
            }

            BeginModify();
            dragTrack_ = trackIndex;
            dragClip_ = ci;
            dragOrigStart_ = clip->start_;
            dragOrigDuration_ = clip->duration_;
            dragGrabOffset_ = XToTime(io.MousePos.x, trackX0) - clip->start_;

            // Snapshot the original start of every selected clip for a batch move.
            dragBatchOrig_.clear();
            for (const auto& sel : multiSelectedClips_)
            {
                if (TimelineClip* selClip = FindSelectedClip(sel.first, sel.second))
                    dragBatchOrig_.push_back({sel, selClip->start_});
            }

            if (io.MousePos.x <= clipX0 + EdgeGrabWidth)
                dragKind_ = DragKind::ClipTrimStart;
            else if (io.MousePos.x >= clipX1 - EdgeGrabWidth)
                dragKind_ = DragKind::ClipTrimEnd;
            else
                dragKind_ = DragKind::ClipMove;
        }
        return true;
    }
    return false;
}

bool TimelineViewTab::ProcessMarkerHitTesting(TimelineMarkerTrack* markerTrack, unsigned trackIndex,
    const ImVec2& rowMin, const ImVec2& rowMax, float trackX0, bool leftClick, bool rightClick)
{
    (void)rightClick;
    ImGuiIO& io = ui::GetIO();
    const auto& markers = markerTrack->GetMarkers();
    for (unsigned mi = 0; mi < markers.size(); ++mi)
    {
        const float x = TimeToX(markers[mi].time_, trackX0);
        const ImVec2 center(x, (rowMin.y + rowMax.y) * 0.5f);
        if (Abs(io.MousePos.x - center.x) <= KeySize + 2.0f && Abs(io.MousePos.y - center.y) <= KeySize + 2.0f)
        {
            if (leftClick)
            {
                SelectTrack(trackIndex);
                selectedKey_ = mi;
                BeginModify();
                dragKind_ = DragKind::Marker;
                dragTrack_ = trackIndex;
                dragKey_ = mi;
                dragOrigStart_ = markers[mi].time_;
                dragGrabOffset_ = XToTime(io.MousePos.x, trackX0) - markers[mi].time_;
            }
            return true;
        }
    }
    return false;
}

bool TimelineViewTab::ProcessPropertyKeyHitTesting(TimelinePropertyTrack* propertyTrack, unsigned trackIndex,
    const ImVec2& rowMin, const ImVec2& rowMax, float trackX0, bool leftClick)
{
    ImGuiIO& io = ui::GetIO();
    const float mouseTime = XToTime(io.MousePos.x, trackX0);
    const float keyY = (rowMin.y + rowMax.y) * 0.5f;

    for (unsigned ci = 0; ci < propertyTrack->clips_.size(); ++ci)
    {
        const auto& propertyClip = propertyTrack->clips_[ci];
        for (float keyTime : GetPropertyKeyTimes(propertyClip))
        {
            const float x = TimeToX(propertyClip.start_ + keyTime, trackX0);
            if (Abs(io.MousePos.x - x) <= KeySize + 2.0f && Abs(io.MousePos.y - keyY) <= KeySize + 2.0f)
            {
                if (leftClick)
                {
                    SelectTrack(trackIndex);
                    selectedClip_ = ci;
                    selectedKeyTime_ = keyTime;
                    BeginModify();
                    dragKind_ = DragKind::Key;
                    dragTrack_ = trackIndex;
                    dragClip_ = ci;
                    dragOrigStart_ = keyTime;
                    dragGrabOffset_ = mouseTime - (propertyClip.start_ + keyTime);
                }
                return true;
            }
        }
    }
    return false;
}

void TimelineViewTab::UpdateDrag(float trackX0)
{
    ImGuiIO& io = ui::GetIO();

    // Horizontal panning with the middle mouse button
    if (dragKind_ != DragKind::None && dragKind_ != DragKind::Pan && !io.MouseDown[0])
        dragKind_ = DragKind::None;
    if (dragKind_ == DragKind::Pan && !io.MouseDown[2])
        dragKind_ = DragKind::None;
    if (dragKind_ == DragKind::None && ui::IsWindowHovered() && ImGui::IsMouseClicked(2))
    {
        dragKind_ = DragKind::Pan;
        dragGrabOffset_ = io.MousePos.x;
        dragOrigStart_ = viewStartTime_;
    }
    if (dragKind_ == DragKind::Pan)
    {
        viewStartTime_ = Max(dragOrigStart_ - (io.MousePos.x - dragGrabOffset_) / pixelsPerSecond_, 0.0f);
        return;
    }

    if (dragKind_ == DragKind::None || !timeline_ || dragTrack_ >= timeline_->GetTracks().size())
        return;

    TimelineTrack* track = timeline_->GetTracks()[dragTrack_].Get();
    const float mouseTime = XToTime(io.MousePos.x, trackX0);
    const bool snapEnabled = !io.KeyCtrl;

    switch (dragKind_)
    {
    case DragKind::ClipMove:
    {
        TimelineClip* clip = GetClip(track, dragClip_);
        if (!clip)
            break;
        const float newStart = Max(SnapTime(mouseTime - dragGrabOffset_, snapEnabled), 0.0f);
        if (dragBatchOrig_.size() > 1)
        {
            // Batch move: shift every selected clip by the same delta so relative spacing holds.
            const float delta = newStart - dragOrigStart_;
            for (auto& [pair, origStart] : dragBatchOrig_)
            {
                if (TimelineClip* selClip = FindSelectedClip(pair.first, pair.second))
                    selClip->start_ = Max(origStart + delta, 0.0f);
            }
        }
        else
        {
            clip->start_ = newStart;
        }
        break;
    }
    case DragKind::ClipTrimStart:
    {
        if (TimelineClip* clip = GetClip(track, dragClip_))
        {
            const float newStart = Clamp(SnapTime(mouseTime, snapEnabled), 0.0f,
                dragOrigStart_ + dragOrigDuration_ - MinClipDuration);
            clip->start_ = newStart;
            clip->duration_ = dragOrigStart_ + dragOrigDuration_ - newStart;
        }
        break;
    }
    case DragKind::ClipTrimEnd:
    {
        if (TimelineClip* clip = GetClip(track, dragClip_))
        {
            const float newEnd = Max(SnapTime(mouseTime, snapEnabled), dragOrigStart_ + MinClipDuration);
            clip->duration_ = Max(newEnd - dragOrigStart_, MinClipDuration);
        }
        break;
    }
    case DragKind::Key:
    {
        if (auto* propertyTrack = AsPropertyTrack(track))
        {
            if (dragClip_ < propertyTrack->clips_.size() && selectedKeyTime_ >= 0.0f)
            {
                auto& propertyClip = propertyTrack->clips_[dragClip_];
                const float localTime = Clamp(SnapTime(mouseTime, snapEnabled) - propertyClip.start_,
                    0.0f, propertyClip.duration_);
                MovePropertyKeysAt(propertyClip, selectedKeyTime_, localTime);
                selectedKeyTime_ = localTime;
            }
        }
        break;
    }
    case DragKind::Marker:
    {
        if (auto* markerTrack = AsMarkerTrack(track))
        {
            if (dragKey_ < markerTrack->GetNumMarkers())
                markerTrack->GetMarkers()[dragKey_].time_ = Max(SnapTime(mouseTime - dragGrabOffset_, snapEnabled), 0.0f);
        }
        break;
    }
    default:
        break;
    }
}

void TimelineViewTab::ProcessDeleteKey()
{
    if (!ui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || !ui::IsKeyPressed(ImGuiKey_Delete)
        || !timeline_ || selectedTrack_ >= timeline_->GetTracks().size())
        return;

    TimelineTrack* track = timeline_->GetTracks()[selectedTrack_].Get();

    // Batch delete of the multi-selected clips (takes precedence over single selection).
    if (multiSelectedClips_.size() > 1)
    {
        BeginModify();
        // Group clip indices per track and erase from the highest index down so earlier
        // erasures do not invalidate the pending ones.
        ea::set<unsigned> tracks;
        for (const auto& sel : multiSelectedClips_)
            tracks.insert(sel.first);
        for (unsigned ti : tracks)
        {
            ea::vector<unsigned> clips;
            for (const auto& sel : multiSelectedClips_)
            {
                if (sel.first == ti)
                    clips.push_back(sel.second);
            }
            // The selection set is ordered, so clips are collected ascending already.
            if (ti < timeline_->GetTracks().size())
            {
                TimelineTrack* t = timeline_->GetTracks()[ti].Get();
                for (auto it = clips.rbegin(); it != clips.rend(); ++it)
                    RemoveClipAt(t, *it);
            }
        }
        multiSelectedClips_.clear();
        selectedClip_ = M_MAX_UNSIGNED;
        return;
    }

    if (auto* markerTrack = AsMarkerTrack(track))
    {
        if (selectedKey_ < markerTrack->GetMarkers().size())
        {
            BeginModify();
            markerTrack->RemoveMarker(selectedKey_);
            selectedKey_ = M_MAX_UNSIGNED;
        }
    }
    else if (auto* propertyTrack = AsPropertyTrack(track))
    {
        if (selectedClip_ < propertyTrack->clips_.size() && selectedKeyTime_ >= 0.0f)
        {
            BeginModify();
            RemovePropertyKeysAt(propertyTrack->clips_[selectedClip_], selectedKeyTime_);
            selectedKeyTime_ = -1.0f;
        }
        else if (selectedClip_ < propertyTrack->clips_.size())
        {
            BeginModify();
            RemoveClipAt(track, selectedClip_);
            selectedClip_ = M_MAX_UNSIGNED;
        }
    }
    else if (selectedClip_ < GetNumClips(track))
    {
        BeginModify();
        RemoveClipAt(track, selectedClip_);
        selectedClip_ = M_MAX_UNSIGNED;
    }
}

void TimelineViewTab::ProcessEditKeys()
{
    if (!ui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || !timeline_)
        return;

    if (ui::GetIO().KeyCtrl)
    {
        if (ui::IsKeyPressed(ImGuiKey_C))
            CopySelectedClips();
        else if (ui::IsKeyPressed(ImGuiKey_V))
            PasteClips(cursorTime_);
    }
}

void TimelineViewTab::RenderTrackAreaContextMenu()
{
    if (!ui::BeginPopup("TrackAreaContextMenu"))
        return;

    const bool snapEnabled = !ui::GetIO().KeyCtrl;
    TimelineTrack* track = timeline_ && popupTrack_ < timeline_->GetTracks().size()
        ? timeline_->GetTracks()[popupTrack_].Get()
        : nullptr;

    if (track)
    {
        if (IsMarkerTrack(track))
        {
            const ea::string label = Format("Add Marker at {}", FormatTime(popupTime_).c_str());
            if (ui::Selectable(label.c_str()))
            {
                BeginModify();
                if (auto* markerTrack = AsMarkerTrack(track))
                {
                    markerTrack->AddMarker(TimelineMarker{MakeUniqueMarkerName(*markerTrack), SnapTime(popupTime_, snapEnabled)});
                    SelectTrack(popupTrack_);
                    selectedKey_ = markerTrack->GetNumMarkers() - 1;
                }
            }
        }
        else if (IsPropertyTrack(track))
        {
            const unsigned targetClip = FindClipAtTime(track, popupTime_);
            const ea::string label = Format("Add Keyframe at {}", FormatTime(popupTime_).c_str());
            auto* propertyTrack = AsPropertyTrack(track);
            const bool canAddKey = targetClip != M_MAX_UNSIGNED && propertyTrack
                && targetClip < propertyTrack->clips_.size()
                && !propertyTrack->clips_[targetClip].curves_.empty();
            ui::BeginDisabled(!canAddKey);
            if (ui::Selectable(label.c_str()) && canAddKey)
            {
                BeginModify();
                auto& propertyClip = propertyTrack->clips_[targetClip];
                const float localTime = Clamp(SnapTime(popupTime_, snapEnabled) - propertyClip.start_, 0.0f, propertyClip.duration_);
                AddPropertyKeyAt(propertyClip, localTime);
                SelectTrack(popupTrack_);
                selectedClip_ = targetClip;
                selectedKeyTime_ = localTime;
            }
            ui::EndDisabled();
            ui::Separator();
            const ea::string clipLabel = Format("Add Clip at {}", FormatTime(popupTime_).c_str());
            if (ui::Selectable(clipLabel.c_str()))
            {
                BeginModify();
                AddDefaultClip(track, SnapTime(popupTime_, snapEnabled));
                SelectTrack(popupTrack_);
                selectedClip_ = GetNumClips(track) - 1;
            }
        }
        else
        {
            const ea::string label = Format("Add Clip at {}", FormatTime(popupTime_).c_str());
            if (ui::Selectable(label.c_str()))
            {
                BeginModify();
                AddDefaultClip(track, SnapTime(popupTime_, snapEnabled));
                SelectTrack(popupTrack_);
                selectedClip_ = GetNumClips(track) - 1;
            }
        }

        ui::Separator();
        if (ui::Selectable("Delete Track"))
        {
            BeginModify();
            timeline_->RemoveTrack(track);
            ClearSelection();
        }
    }
    ui::EndPopup();
}

void TimelineViewTab::RenderClipContextMenu()
{
    if (!ui::BeginPopup("ClipContextMenu"))
        return;

    TimelineTrack* track = timeline_ && popupTrack_ < timeline_->GetTracks().size()
        ? timeline_->GetTracks()[popupTrack_].Get()
        : nullptr;
    if (track && popupClip_ < GetNumClips(track))
    {
        if (ui::Selectable("Delete Clip"))
        {
            BeginModify();
            RemoveClipAt(track, popupClip_);
            selectedClip_ = M_MAX_UNSIGNED;
            selectedKeyTime_ = -1.0f;
        }
        if (ui::Selectable("Duplicate Clip"))
        {
            BeginModify();
            DuplicateClipAt(track, popupClip_);
            SelectTrack(popupTrack_);
            selectedClip_ = GetNumClips(track) - 1;
        }
    }
    ui::EndPopup();
}

void TimelineViewTab::RenderTrackHeaderContextMenu()
{
    if (!ui::BeginPopup("TrackHeaderContextMenu"))
        return;

    TimelineTrack* track = timeline_ && popupTrack_ < timeline_->GetTracks().size()
        ? timeline_->GetTracks()[popupTrack_].Get()
        : nullptr;
    if (track)
    {
        if (ui::Selectable(track->IsMuted() ? "Unmute Track" : "Mute Track"))
        {
            BeginModify();
            track->SetMuted(!track->IsMuted());
        }
        if (ui::Selectable("Delete Track"))
        {
            BeginModify();
            timeline_->RemoveTrack(track);
            ClearSelection();
        }
    }
    ui::EndPopup();
}

bool TimelineViewTab::EditString(const char* label, ea::string& value)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ui::InputText(label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        BeginModify();
        value = buffer;
        return true;
    }
    return false;
}

void TimelineViewTab::RenderSelectionPanel()
{
    ui::Separator();
    ui::BeginChild("timeline_selection", ImVec2(ui::GetContentRegionAvail().x, 0.0f), ImGuiChildFlags_None, 0);

    if (!timeline_ || selectedTrack_ >= timeline_->GetNumTracks())
    {
        ui::TextDisabled("Select a track to edit its properties");
        ui::EndChild();
        return;
    }

    TimelineTrack* track = timeline_->GetTrack(selectedTrack_);

    RenderTrackProperties(track);
    RenderClipProperties(track);
    RenderMarkerProperties(track);
    RenderPropertyKeyAndCurves(track);

    ui::EndChild();
}

void TimelineViewTab::RenderTrackProperties(TimelineTrack* track)
{
    ui::TextDisabled("Track");
    ui::SameLine();
    ui::SetNextItemWidth(140.0f);
    {
        ea::string name = track->GetName();
        if (EditString("##trackName", name))
            track->SetName(name);
    }
    ui::SameLine();
    ui::SetNextItemWidth(200.0f);
    {
        ea::string bindingPath = track->GetBindingPath();
        if (EditString("##trackBinding", bindingPath))
            track->SetBindingPath(bindingPath);
    }
    ui::SameLine();
    ui::SetNextItemWidth(110.0f);
    {
        float weight = track->GetWeight();
        if (ui::SliderFloat("Weight", &weight, 0.0f, 1.0f, "%.2f"))
        {
            BeginModify();
            track->SetWeight(weight);
        }
    }
    ui::SameLine();
    {
        bool muted = track->IsMuted();
        if (ui::Checkbox("Muted", &muted))
        {
            BeginModify();
            track->SetMuted(muted);
        }
    }
}

void TimelineViewTab::RenderClipProperties(TimelineTrack* track)
{
    TimelineClip* clip = GetClip(track, selectedClip_);
    if (!clip)
        return;

    ui::Separator();
    ui::TextDisabled("Clip");
    ui::SameLine();
    ui::SetNextItemWidth(160.0f);
    {
        ea::string clipName = clip->name_;
        if (EditString("##clipName", clipName))
            clip->name_ = clipName;
    }
    ui::SameLine();
    ui::SetNextItemWidth(110.0f);
    {
        float start = clip->start_;
        if (ui::DragFloat("Start", &start, 0.01f, 0.0f, FLT_MAX, "%.2f s"))
        {
            BeginModify();
            clip->start_ = Max(start, 0.0f);
        }
    }
    ui::SameLine();
    ui::SetNextItemWidth(110.0f);
    {
        float clipDuration = clip->duration_;
        if (ui::DragFloat("Duration", &clipDuration, 0.01f, MinClipDuration, FLT_MAX, "%.2f s"))
        {
            BeginModify();
            clip->duration_ = Max(clipDuration, MinClipDuration);
        }
    }
}

void TimelineViewTab::RenderMarkerProperties(TimelineTrack* track)
{
    auto* markerTrack = AsMarkerTrack(track);
    if (!markerTrack || selectedKey_ >= markerTrack->GetNumMarkers())
        return;

    auto& marker = markerTrack->GetMarkers()[selectedKey_];
    ui::Separator();
    ui::TextDisabled("Marker");
    ui::SameLine();
    ui::SetNextItemWidth(160.0f);
    {
        ea::string markerName = marker.name_;
        if (EditString("##markerName", markerName))
            marker.name_ = markerName;
    }
    ui::SameLine();
    ui::SetNextItemWidth(110.0f);
    {
        float markerTime = marker.time_;
        if (ui::DragFloat("Time", &markerTime, 0.01f, 0.0f, FLT_MAX, "%.2f s"))
        {
            BeginModify();
            marker.time_ = Max(markerTime, 0.0f);
        }
    }
}

void TimelineViewTab::RenderPropertyKeyAndCurves(TimelineTrack* track)
{
    auto* propertyTrack = AsPropertyTrack(track);
    if (!propertyTrack || selectedClip_ >= propertyTrack->clips_.size())
        return;

    auto& propertyClip = propertyTrack->clips_[selectedClip_];

    if (selectedKeyTime_ >= 0.0f)
    {
        ui::Separator();
        ui::TextDisabled("Keyframe");
        ui::SameLine();
        ui::SetNextItemWidth(110.0f);
        float keyTime = selectedKeyTime_;
        if (ui::DragFloat("Key time", &keyTime, 0.01f, 0.0f, propertyClip.duration_, "%.2f s"))
        {
            BeginModify();
            MovePropertyKeysAt(propertyClip, selectedKeyTime_, keyTime);
            selectedKeyTime_ = keyTime;
        }

        // Interpolation / tangent editing applies to the time-aligned key of every curve in the
        // clip (the sequencer treats a keyframe row at one time as that key across all curves).
        // A null mutation entry means no curve has a key at the selected time; guarded below.
        constexpr int interpCount = static_cast<int>(sizeof(TIMELINE_INTERPOLATION_NAMES) / sizeof(const char*)) - 1;
        constexpr int easeCount = static_cast<int>(sizeof(TIMELINE_EASE_NAMES) / sizeof(const char*)) - 1;

        // Representative values from the first curve that owns a key at this time, for widget init.
        TimelinePropertyKey* rep = nullptr;
        for (auto& curve : propertyClip.curves_)
        {
            for (auto& key : curve.keys_)
            {
                if (key.time_ == selectedKeyTime_)
                {
                    rep = &key;
                    break;
                }
            }
            if (rep)
                break;
        }

        if (rep)
        {
            const auto applyToSelectedKeys = [&propertyClip, this](const auto& fn)
            {
                BeginModify();
                for (auto& curve : propertyClip.curves_)
                    for (auto& key : curve.keys_)
                        if (key.time_ == selectedKeyTime_)
                            fn(key);
            };

            int interp = static_cast<int>(rep->interpolation_);
            ui::SameLine();
            ui::SetNextItemWidth(110.0f);
            if (ui::Combo("##keyInterp", &interp, TIMELINE_INTERPOLATION_NAMES, interpCount))
                applyToSelectedKeys([interp](TimelinePropertyKey& key) { key.interpolation_ = static_cast<TimelineInterpolation>(interp); });
            ui::SameLine();
            ui::TextDisabled("Interp");

            if (rep->interpolation_ == TimelineInterpolation::Ease)
            {
                int ease = static_cast<int>(rep->ease_);
                ui::SameLine();
                ui::SetNextItemWidth(110.0f);
                if (ui::Combo("##keyEase", &ease, TIMELINE_EASE_NAMES, easeCount))
                    applyToSelectedKeys([ease](TimelinePropertyKey& key) { key.ease_ = static_cast<TimelineEase>(ease); });
            }
            else if (rep->interpolation_ == TimelineInterpolation::Bezier)
            {
                ui::SameLine();
                ui::SetNextItemWidth(110.0f);
                float outTangent = rep->outTangent_;
                if (ui::DragFloat("##outTangent", &outTangent, 0.01f))
                    applyToSelectedKeys([outTangent](TimelinePropertyKey& key) { key.outTangent_ = outTangent; });
                ui::SameLine();
                ui::TextDisabled("Out");
                ui::SameLine();
                ui::SetNextItemWidth(110.0f);
                float inTangent = rep->inTangent_;
                if (ui::DragFloat("##inTangent", &inTangent, 0.01f))
                    applyToSelectedKeys([inTangent](TimelinePropertyKey& key) { key.inTangent_ = inTangent; });
                ui::SameLine();
                ui::TextDisabled("In");
            }
        }
    }

    ui::SeparatorText("Curves");
    for (unsigned ci = 0; ci < propertyClip.curves_.size(); ++ci)
    {
        auto& curve = propertyClip.curves_[ci];
        ui::PushID(static_cast<int>(ci));
        ui::SetNextItemWidth(160.0f);
        EditString("##curveName", curve.name_);
        ui::SameLine();
        ui::TextDisabled("%s", Variant::GetTypeName(curve.valueType_).c_str());
        ui::SameLine();
        if (ui::Button("Remove"))
        {
            BeginModify();
            propertyClip.curves_.erase(propertyClip.curves_.begin() + static_cast<long>(ci));
            selectedKeyTime_ = -1.0f;
        }
        ui::PopID();
    }

    ui::SetNextItemWidth(160.0f);
    ui::InputText("##newCurveName", newCurveName_, sizeof(newCurveName_));
    ui::SameLine();
    ui::SetNextItemWidth(100.0f);
    ui::Combo("##newCurveType", &newCurveType_, curveTypeNames, sizeof(curveTypeNames) / sizeof(curveTypeNames[0]));
    ui::SameLine();
    if (ui::Button("Add Curve"))
    {
        const ea::string curveName = newCurveName_;
        if (!curveName.empty() && newCurveType_ >= 0
            && newCurveType_ < static_cast<int>(sizeof(curveTypes) / sizeof(curveTypes[0])))
        {
            BeginModify();
            TimelinePropertyCurve curve;
            curve.name_ = curveName;
            curve.valueType_ = curveTypes[newCurveType_];
            // A single key at the clip start with the default value of the type
            TimelinePropertyKey key;
            key.time_ = 0.0f;
            key.value_ = Variant(curve.valueType_);
            curve.keys_.push_back(key);
            propertyClip.curves_.push_back(curve);
            newCurveName_[0] = '\0';
        }
    }
}

}
