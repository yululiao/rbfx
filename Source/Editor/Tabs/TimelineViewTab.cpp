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

#include "../Timeline/TimelineCurveUtils.h"
#include "../Timeline/TimelineEditorCommon.h"
#include "../Timeline/TimelineTrackRows.h"

#include "../Project/ModifyResourceAction.h"
#include "../Project/Project.h"
#include "../Project/ProjectRequest.h"

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Audio/Sound.h>
#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Texture2D.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/RenderAPI/RenderDevice.h>
#include <Urho3D/Scene/PrefabResource.h>
#include <Urho3D/Scene/Serializable.h>
#include <Urho3D/Scene/Component.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/DragDropPayload.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/SystemUI/Widgets.h>
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

#include <EASTL/algorithm.h>
#include <EASTL/set.h>

#include <nfd.h>

#include <cfloat>
#include <cstdio>
#include <type_traits>

namespace Urho3D
{

namespace
{

// Layout constants.
// Base* are the design values at 100% DPI (96). The runtime copies below are refreshed every
// frame by ApplyLayoutScale() so that track rows, the ruler, keys and paddings keep a constant
// physical size on displays with different DPI. Row height was also raised versus the old 26/20
// because a single track felt too cramped.
constexpr float BaseHeaderWidth = 260.0f;
constexpr float BaseRulerHeight = 28.0f;
constexpr float BaseFrameRulerHeight = 20.0f;
constexpr float BaseRowHeight = 40.0f;
constexpr float BaseThinRowHeight = 30.0f;
constexpr float BaseRowSpacing = 3.0f;
constexpr float BaseClipRounding = 3.0f;
constexpr float BaseEdgeGrabWidth = 8.0f;
constexpr float BaseKeySize = 8.0f;
constexpr float BaseBindingSlotWidth = 76.0f;
constexpr float BaseZoomBarHeight = 9.0f;
constexpr float BaseRowIndent = 14.0f;
constexpr float BaseMrbButtonSize = 18.0f;
constexpr float BaseEaseHandleMin = 12.0f;
constexpr float BaseLoopLaneHeight = 9.0f;
constexpr float BaseMarkersStripHeight = 34.0f;
constexpr float BaseMarkersPanelHeight = 120.0f;

// DPI-scaled copies actually used by layout, rendering and hit-testing. They stay consistent
// across all of them because every row/hit rect is derived from these same values.
float HeaderWidth = BaseHeaderWidth;
float RulerHeight = BaseRulerHeight;
float FrameRulerHeight = BaseFrameRulerHeight;
float RowHeight = BaseRowHeight;
float ThinRowHeight = BaseThinRowHeight;
float RowSpacing = BaseRowSpacing;
float ClipRounding = BaseClipRounding;
float EdgeGrabWidth = BaseEdgeGrabWidth;
float KeySize = BaseKeySize;
float BindingSlotWidth = BaseBindingSlotWidth;
float ZoomBarHeight = BaseZoomBarHeight;
float RowIndent = BaseRowIndent;
float MrbButtonSize = BaseMrbButtonSize;
float EaseHandleMin = BaseEaseHandleMin;
float LoopLaneHeight = BaseLoopLaneHeight;
float MarkersStripHeight = BaseMarkersStripHeight;
float MarkersPanelHeight = BaseMarkersPanelHeight;

// Time/zoom constants: not physical sizes, so they are not DPI-scaled.
constexpr float MinClipDuration = 0.05f;
constexpr float MinPixelsPerSecond = 5.0f;
constexpr float MaxPixelsPerSecond = 2000.0f;
constexpr float MinRulerStepPx = 90.0f;
// Distance in screen pixels within which a clip edge or a keyframe grabs a drag. A physical
// tolerance keeps snapping the same at every zoom level, unlike a fixed time window.
constexpr float SnapTolerancePx = 8.0f;

// Multiply the geometric layout constants by the display DPI scale so the on-screen physical
// size stays consistent. Called once per frame before the sequencer is laid out.
void ApplyLayoutScale(float scale)
{
    HeaderWidth = BaseHeaderWidth * scale;
    RulerHeight = BaseRulerHeight * scale;
    FrameRulerHeight = BaseFrameRulerHeight * scale;
    RowHeight = BaseRowHeight * scale;
    ThinRowHeight = BaseThinRowHeight * scale;
    RowSpacing = BaseRowSpacing * scale;
    ClipRounding = BaseClipRounding * scale;
    EdgeGrabWidth = BaseEdgeGrabWidth * scale;
    KeySize = BaseKeySize * scale;
    BindingSlotWidth = BaseBindingSlotWidth * scale;
    ZoomBarHeight = BaseZoomBarHeight * scale;
    RowIndent = BaseRowIndent * scale;
    MrbButtonSize = BaseMrbButtonSize * scale;
    EaseHandleMin = BaseEaseHandleMin * scale;
    LoopLaneHeight = BaseLoopLaneHeight * scale;
    MarkersStripHeight = BaseMarkersStripHeight * scale;
    MarkersPanelHeight = BaseMarkersPanelHeight * scale;
}

// Truncate text with an ellipsis so it fits within maxWidth pixels. Used for clip labels so a
// very long animation name no longer spills past its block and overlaps the neighbouring clip.
ea::string FitText(const char* text, float maxWidth)
{
    ea::string result = text;
    if (ui::CalcTextSize(result.c_str()).x <= maxWidth)
        return result;
    const ea::string ellipsis = "...";
    while (!result.empty() && ui::CalcTextSize((result + ellipsis).c_str()).x > maxWidth)
        result.pop_back();
    return result.empty() ? ellipsis : result + ellipsis;
}

// Number of selectable entries of a null terminated name table like TIMELINE_EASE_NAMES: the
// trailing nullptr marks the end of the array and must not be offered as an item.
template <unsigned N>
constexpr unsigned NameCount(const char* const (&names)[N])
{
    return N - 1;
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

bool IsGroupTrack(const TimelineTrack* track)
{
    return dynamic_cast<const TimelineGroupTrack*>(track) != nullptr;
}

float GetRowHeight(const TimelineTrack* track)
{
    // Marker lanes and group organizers carry no clips, so they are given the thinner row.
    return IsMarkerTrack(track) || IsGroupTrack(track) ? ThinRowHeight : RowHeight;
}

// Left margin geometry of a row: the indentation of the nesting level, the expand/collapse
// control of a group track and the inline mute/solo/lock controls. Both the header renderer and
// the row hit-testing measure the same rects from these values, so clicking a control can never
// also start a selection or a reorder drag.
struct RowMargin
{
    /// Horizontal offset consumed by the nesting of the row.
    float indent_{};
    /// Expand/collapse control of a group row, degenerate for any other track.
    ImVec2 toggleMin_{};
    ImVec2 toggleMax_{};
    /// Mute, solo and edit lock buttons of the row.
    ImVec2 muteMin_{};
    ImVec2 muteMax_{};
    ImVec2 soloMin_{};
    ImVec2 soloMax_{};
    ImVec2 lockMin_{};
    ImVec2 lockMax_{};
    /// Where the type color bar of the row starts.
    float colorBarX_{};
    /// Where the type caption starts.
    float captionX_{};
    /// Right edge of the inline controls: the track name is clipped between it and the slot.
    float controlsEnd_{};
};

RowMargin ComputeRowMargin(const ImVec2& rowMin, const ImVec2& rowMax, const TrackRow& row)
{
    RowMargin margin;
    margin.indent_ = static_cast<float>(row.depth_) * RowIndent;
    margin.colorBarX_ = rowMin.x + margin.indent_;
    margin.captionX_ = margin.colorBarX_ + 10.0f;
    float x = margin.colorBarX_ + 8.0f;

    if (row.isGroup_)
    {
        const float arrowSize = 10.0f;
        const float centerY = (rowMin.y + rowMax.y) * 0.5f;
        margin.toggleMin_ = ImVec2(x, centerY - arrowSize * 0.5f);
        margin.toggleMax_ = ImVec2(x + arrowSize + 6.0f, centerY + arrowSize * 0.5f);
        x = margin.toggleMax_.x + 2.0f;
    }

    // The three controls are square, vertically centered in the row and laid out left to right.
    const float buttonTop = (rowMin.y + rowMax.y) * 0.5f - MrbButtonSize * 0.5f;
    const float buttonBottom = buttonTop + MrbButtonSize;
    margin.muteMin_ = ImVec2(x, buttonTop);
    margin.muteMax_ = ImVec2(x + MrbButtonSize, buttonBottom);
    x += MrbButtonSize + 2.0f;
    margin.soloMin_ = ImVec2(x, buttonTop);
    margin.soloMax_ = ImVec2(x + MrbButtonSize, buttonBottom);
    x += MrbButtonSize + 2.0f;
    margin.lockMin_ = ImVec2(x, buttonTop);
    margin.lockMax_ = ImVec2(x + MrbButtonSize, buttonBottom);
    x += MrbButtonSize;

    margin.controlsEnd_ = x;
    margin.captionX_ = x + 6.0f;
    return margin;
}

// Look of the inline header controls. The active state is carried by the background tint, the
// letter stays readable either way.
const ImU32 ColorMrbBg = IM_COL32(48, 48, 48, 255);
const ImU32 ColorMrbBorder = IM_COL32(120, 120, 120, 200);
const ImU32 ColorMrbText = IM_COL32(187, 187, 187, 255);
const ImU32 ColorMrbMute = IM_COL32(216, 84, 60, 255);
const ImU32 ColorMrbSolo = IM_COL32(224, 176, 48, 255);
const ImU32 ColorMrbLock = IM_COL32(96, 168, 216, 255);

// Look of the ease ramps drawn into the corners of a clip block.
const ImU32 ColorEaseHandle = IM_COL32(255, 255, 255, 48);
const ImU32 ColorEaseCurve = IM_COL32(255, 255, 255, 190);
const ImU32 ColorBlendRegion = IM_COL32(0, 0, 0, 70);
const ImU32 ColorBlendBorder = IM_COL32(255, 255, 255, 120);
// Loop region of the lane above the ruler: the region reads as a bracket, its edges as handles.
// An inactive region is drawn in the same colors at a fraction of the alpha, so that it stays
// visible as the remembered edit without looking like the preview is looping.
const ImU32 ColorLoopLaneBg = IM_COL32(50, 46, 46, 255);
const ImU32 ColorLoopRegion = IM_COL32(216, 130, 0, 70);
const ImU32 ColorLoopBorder = IM_COL32(216, 130, 0, 220);
const ImU32 ColorLoopHandle = IM_COL32(240, 180, 90, 255);

// Grab geometry of the ease ramps of a clip block. The ease-in ramp is the triangle at the top
// left corner, the ease-out ramp the one at the bottom right; the horizontal leg of each is the
// length of the ramp in time. Both are inset by the trim grab width so dragging a ramp never
// collides with trimming the block. RenderTrackContent draws exactly these triangles and
// ProcessClipHitTesting hits exactly these rects.
struct EaseHandles
{
    ImVec2 inMin_{};
    ImVec2 inMax_{};
    ImVec2 outMin_{};
    ImVec2 outMax_{};
    float inWidth_{};
    float outWidth_{};
};

// Ease ramps, the clip weight and the overlap blend are all read through
// TimelineClip::EvaluateWeight, and only the tracks that mix their content by that weight call
// it: animation blends its asset, audio fades its gain, property clips blend over each other.
// Activation is an on/off range, control a playable range and signal an instant, so their clips
// carry the fields but never consult them, and the editor offers no handle, grab, slider or blend
// mark for something that cannot take effect.
bool TrackBlendsByWeight(const TimelineTrack* track)
{
    return dynamic_cast<const TimelineAnimationTrack*>(track) != nullptr
        || dynamic_cast<const TimelineAudioTrack*>(track) != nullptr
        || dynamic_cast<const TimelinePropertyTrack*>(track) != nullptr;
}

EaseHandles ComputeEaseHandles(const ImVec2& blockMin, const ImVec2& blockMax, const TimelineClip& clip)
{
    EaseHandles handles;
    const float width = Max(blockMax.x - blockMin.x, 1.0f);
    const float yMid = (blockMin.y + blockMax.y) * 0.5f;
    const float duration = Max(clip.duration_, MinClipDuration);
    const float grabInset = Min(EdgeGrabWidth, width * 0.25f);

    handles.inWidth_ = Min(Max(width * clip.easeIn_ / duration, EaseHandleMin), width);
    handles.outWidth_ = Min(Max(width * clip.easeOut_ / duration, EaseHandleMin), width);
    handles.inMin_ = ImVec2(blockMin.x + grabInset, blockMin.y);
    handles.inMax_ = ImVec2(blockMin.x + grabInset + handles.inWidth_, yMid);
    handles.outMin_ = ImVec2(blockMax.x - grabInset - handles.outWidth_, yMid);
    handles.outMax_ = ImVec2(blockMax.x - grabInset, blockMax.y);
    return handles;
}

void DrawEaseHandle(ImDrawList* drawList, const EaseHandles& handles, const TimelineClip& clip)
{
    // Ease in: right angle at the top left, the ramp rises over its own length.
    drawList->AddTriangleFilled(handles.inMin_, ImVec2(handles.inMin_.x + handles.inWidth_, handles.inMin_.y),
        ImVec2(handles.inMin_.x, handles.inMax_.y), ColorEaseHandle);
    for (unsigned i = 0; i < 8; ++i)
    {
        const float t0 = static_cast<float>(i) / 8.0f;
        const float t1 = static_cast<float>(i + 1) / 8.0f;
        drawList->AddLine(
            ImVec2(handles.inMin_.x + t0 * handles.inWidth_, handles.inMax_.y - EvaluateTimelineEase(clip.ease_, t0) * (handles.inMax_.y - handles.inMin_.y)),
            ImVec2(handles.inMin_.x + t1 * handles.inWidth_, handles.inMax_.y - EvaluateTimelineEase(clip.ease_, t1) * (handles.inMax_.y - handles.inMin_.y)),
            ColorEaseCurve, 1.0f);
    }

    // Ease out: mirrored at the bottom right, the ramp falls over its own length.
    drawList->AddTriangleFilled(handles.outMax_, ImVec2(handles.outMax_.x - handles.outWidth_, handles.outMax_.y),
        ImVec2(handles.outMax_.x, handles.outMin_.y), ColorEaseHandle);
    for (unsigned i = 0; i < 8; ++i)
    {
        const float t0 = static_cast<float>(i) / 8.0f;
        const float t1 = static_cast<float>(i + 1) / 8.0f;
        drawList->AddLine(
            ImVec2(handles.outMax_.x - t0 * handles.outWidth_, handles.outMin_.y + EvaluateTimelineEase(clip.ease_, t0) * (handles.outMax_.y - handles.outMin_.y)),
            ImVec2(handles.outMax_.x - t1 * handles.outWidth_, handles.outMin_.y + EvaluateTimelineEase(clip.ease_, t1) * (handles.outMax_.y - handles.outMin_.y)),
            ColorEaseCurve, 1.0f);
    }
}


// Draw one of the inline header controls. The letter is the only affordance, the tint of the
// button is the state; hovering shows a tooltip explaining what clicking it does.
void DrawMrbButton(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* label, bool active,
    ImU32 activeColor)
{
    drawList->AddRectFilled(min, max, active ? activeColor : ColorMrbBg, 3.0f);
    drawList->AddRect(min, max, active ? activeColor : ColorMrbBorder, 3.0f, 0, 1.0f);
    const ImVec2 size = ui::CalcTextSize(label);
    drawList->AddText(ImVec2(min.x + (max.x - min.x - size.x) * 0.5f, min.y + (max.y - min.y - size.y) * 0.5f),
        active ? IM_COL32(24, 24, 24, 255) : ColorMrbText, label);
}

ImU32 GetTrackColor(const TimelineTrack* track)
{
    // The same tint is used by the sequencer rows, the header color bar and the dope sheet.
    return GetTrackTypeColor(track->GetTrackTypeName());
}

// Generic clip access. The type specific dispatch of the clip vectors lives in TimelineTrackRows,
// which the dope sheet and the curve editor share with this tab.
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

// Whether the clip shares its time range with another clip of the same track.
bool HasClipOverlap(const TimelineTrack* track, unsigned index)
{
    const TimelineClip* clip = GetClipAt(track, index);
    if (!clip)
        return false;

    const unsigned numClips = GetNumClips(track);
    for (unsigned other = 0; other < numClips; ++other)
    {
        if (other == index)
            continue;
        const TimelineClip* next = GetClipAt(track, other);
        if (next && next->start_ < clip->GetEndTime() && clip->start_ < next->GetEndTime())
            return true;
    }
    return false;
}

// Whether the ease ramps of one clip can reach the evaluation. The track has to blend by weight,
// and a property clip needs an overlapping neighbour: while it stands alone its value is picked
// verbatim, so no ramp can change the result. Animation and audio fade their own content, which is
// why they keep the ramps without an overlap.
bool ClipShowsEase(const TimelineTrack* track, unsigned index)
{
    if (!TrackBlendsByWeight(track))
        return false;
    if (dynamic_cast<const TimelinePropertyTrack*>(track))
        return HasClipOverlap(track, index);
    return true;
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

// Clip editing primitives of the Unity style interactions: split, ripple delete, gap removal and
// speed. They only change the data of the track, the caller owns the undo action.

// Two key times closer than this are the same key. The cut position comes from the cursor, so an
// exact comparison would duplicate a key right on the boundary.
constexpr float KeyTimeEps = 0.0001f;

// Drop the keys of a curve on one side of a split point: keepAfter retains the keys past the cut.
void EraseKeysOutside(ea::vector<TimelinePropertyKey>& keys, float cut, bool keepAfter)
{
    for (auto iter = keys.begin(); iter != keys.end();)
    {
        const bool remove = keepAfter ? iter->time_ < cut - KeyTimeEps : iter->time_ > cut + KeyTimeEps;
        iter = remove ? keys.erase(iter) : ea::next(iter);
    }
}

// Append a key holding the value the curve evaluates at the given local time. Used by the split so
// the two halves end and start on the same value the player was showing before the cut.
void AddContinuousKey(TimelinePropertyCurve& curve, float localTime)
{
    for (const auto& key : curve.keys_)
    {
        if (Abs(key.time_ - localTime) < KeyTimeEps)
            return;
    }
    if (curve.keys_.empty())
        return;

    // Sample first, the curve must still describe the uncut content.
    TimelinePropertyKey key;
    key.time_ = localTime;
    const int count = GetCurveComponentCount(curve.valueType_);
    float slope = 0.0f;
    for (int component = 0; component < count; ++component)
    {
        const float value = EvaluateCurveComponent(curve, localTime, component);
        if (component == 0)
        {
            // Finite difference of the first component: tangents are scalar in this data model, the
            // runtime applies them to every component.
            const float eps = 0.001f;
            const float before = EvaluateCurveComponent(curve, Max(localTime - eps, 0.0f), 0);
            const float after = EvaluateCurveComponent(curve, localTime + eps, 0);
            slope = (after - before) / (2.0f * eps);
        }
        if (component < 4)
            SetCurveComponentValue(key.value_, component, value);
    }

    // Types without a numeric representation cannot be sampled: inherit the nearest key instead.
    if (count <= 0)
    {
        const TimelinePropertyKey* nearest = nullptr;
        float bestDist = FLT_MAX;
        for (const auto& candidate : curve.keys_)
        {
            const float dist = Abs(candidate.time_ - localTime);
            if (dist < bestDist)
            {
                bestDist = dist;
                nearest = &candidate;
            }
        }
        if (nearest)
            key.value_ = nearest->value_;
    }

    // Follow the segment the cut falls into: the boundary key keeps the interpolation and easing of
    // the key that precedes it, so the split does not restyle the curve.
    const TimelinePropertyKey* previous = nullptr;
    for (const auto& candidate : curve.keys_)
    {
        if (candidate.time_ <= localTime)
            previous = &candidate;
        else
            break;
    }
    if (previous)
    {
        key.interpolation_ = previous->interpolation_;
        key.ease_ = previous->ease_;
        if (key.interpolation_ == TimelineInterpolation::Bezier)
        {
            // A cubic segment is reproduced exactly by the slope it has at the cut.
            key.inTangent_ = key.outTangent_ = slope;
        }
    }
    else if (!curve.keys_.empty())
    {
        key.interpolation_ = curve.keys_.front().interpolation_;
        key.ease_ = curve.keys_.front().ease_;
    }

    unsigned insertAt = 0;
    while (insertAt < curve.keys_.size() && curve.keys_[insertAt].time_ < localTime)
        ++insertAt;
    curve.keys_.insert(curve.keys_.begin() + static_cast<long>(insertAt), key);
}

// Split the clip into two clips of the same type at the given timeline time. Returns the index of
// the inserted second half, or M_MAX_UNSIGNED when the cut does not fall inside the clip.
unsigned SplitClipAt(TimelineTrack* track, unsigned index, float splitTime)
{
    unsigned result = M_MAX_UNSIGNED;
    DispatchClipVector(track, [&](auto& clips)
    {
        if (index >= clips.size())
            return;
        using ClipType = typename std::remove_reference_t<decltype(clips)>::value_type;
        ClipType& head = clips[index];
        const float cut = splitTime - head.start_;
        if (cut <= MinClipDuration || head.duration_ - cut <= MinClipDuration)
            return;

        ClipType tail = head;
        tail.start_ = splitTime;
        tail.duration_ = head.duration_ - cut;
        // The content of the tail begins where the cut consumes the source at its own rate.
        tail.clipIn_ = head.clipIn_ + cut * head.timeScale_;
        // Ease ramps are measured from the ends of the clip they belong to, so whatever part of a
        // ramp falls into the other half is dropped from this one.
        tail.easeIn_ = Max(head.easeIn_ - cut, 0.0f);
        head.easeOut_ = Max(head.easeOut_ - (head.duration_ - cut), 0.0f);
        head.duration_ = cut;

        if constexpr (std::is_same_v<ClipType, TimelinePropertyClip>)
        {
            for (auto& curve : head.curves_)
            {
                EraseKeysOutside(curve.keys_, cut, false);
                AddContinuousKey(curve, cut);
            }
            for (auto& curve : tail.curves_)
            {
                EraseKeysOutside(curve.keys_, cut, true);
                for (auto& key : curve.keys_)
                    key.time_ -= cut;
                AddContinuousKey(curve, 0.0f);
            }
        }

        clips.insert(clips.begin() + static_cast<long>(index) + 1, tail);
        result = index + 1;
    });
    return result;
}

// Delete the clip and pull every clip that started after it left by its duration, closing the gap
// the deletion would otherwise leave.
void RippleDeleteClipAt(TimelineTrack* track, unsigned index)
{
    DispatchClipVector(track, [index](auto& clips)
    {
        if (index >= clips.size())
            return;
        const float start = clips[index].start_;
        const float duration = clips[index].duration_;
        clips.erase(clips.begin() + static_cast<long>(index));
        for (auto& clip : clips)
        {
            if (clip.start_ > start)
                clip.start_ = Max(clip.start_ - duration, 0.0f);
        }
    });
}

// Move every clip left until it touches the clip that ends last before it. The stored order of the
// clips is kept, only their start times change: the runtime may weight overlapping clips of the
// same track by their position in the list.
void EliminateGaps(TimelineTrack* track)
{
    DispatchClipVector(track, [](auto& clips)
    {
        ea::vector<unsigned> order(clips.size());
        for (unsigned i = 0; i < clips.size(); ++i)
            order[i] = i;
        ea::sort(order.begin(), order.end(), [&clips](unsigned a, unsigned b)
        {
            return clips[a].start_ < clips[b].start_;
        });

        float cursor = 0.0f;
        for (unsigned i : order)
        {
            auto& clip = clips[i];
            if (clip.start_ > cursor)
                clip.start_ = cursor;
            cursor = Max(cursor, clip.GetEndTime());
        }
    });
}

// Change the playback speed of a clip. Either the block keeps its length in the timeline and the
// content plays faster, or the source span it consumes is preserved and the block shrinks.
void SetClipSpeed(TimelineTrack* track, unsigned index, float value, bool keepDuration)
{
    const float speed = Clamp(value, 0.05f, 20.0f);
    DispatchClipVector(track, [index, speed, keepDuration](auto& clips)
    {
        if (index >= clips.size())
            return;
        auto& clip = clips[index];
        if (keepDuration)
        {
            clip.timeScale_ = speed;
        }
        else
        {
            const float sourceSpan = clip.duration_ * clip.timeScale_;
            clip.timeScale_ = speed;
            clip.duration_ = Max(sourceSpan / speed, MinClipDuration);
        }
    });
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

// Find attribute of the target by exact name, used by preview and recording.
const AttributeInfo* FindAttributeByName(const Serializable& target, const ea::string& name)
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
Object* ResolveTrackTarget(TimelinePlayer* player, TimelineTrack* track, Object* bindingRoot = nullptr)
{
    if (!player || !track)
        return nullptr;

    ea::unique_ptr<TimelineTrackRuntime> runtime = track->CreateRuntime();
    return player->ResolveTrackBinding(*track, *runtime, bindingRoot);
}

// Resolve the binding root a track inherits from the groups enclosing it: the object the nearest
// group ancestor is bound to. Top level tracks inherit nothing and resolve against the player node.
Object* ResolveInheritedBindingRoot(TimelinePlayer* player, const TimelineResource* timeline, const TrackRef& ref)
{
    if (!player || !timeline)
        return nullptr;

    Object* root = nullptr;
    for (unsigned depth = 1; depth < ref.Depth(); ++depth)
    {
        TimelineTrack* ancestor = TrackAt(timeline, ref.Prefix(depth));
        if (!ancestor)
            break;
        root = ResolveTrackTarget(player, ancestor, root);
    }
    return root;
}

// Resolve the object a track of the displayed timeline is bound to, including the binding it
// inherits from its enclosing groups. Used by the preview, the recorder and the header tooltips.
Object* ResolveBoundTarget(TimelinePlayer* player, const TimelineResource* timeline, const TrackRef& ref,
    TimelineTrack* track)
{
    return ResolveTrackTarget(player, track, ResolveInheritedBindingRoot(player, timeline, ref));
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
// Returns whether a new key was inserted.
bool SetCurveKeyValue(TimelinePropertyCurve& curve, float localTime, const Variant& value)
{
    for (TimelinePropertyKey& key : curve.keys_)
    {
        if (Abs(key.time_ - localTime) < 0.001f)
        {
            key.value_ = value;
            return false;
        }
    }

    TimelinePropertyKey newKey;
    newKey.time_ = localTime;
    newKey.value_ = value;

    unsigned insertAt = 0;
    while (insertAt < curve.keys_.size() && curve.keys_[insertAt].time_ < localTime)
        ++insertAt;
    curve.keys_.insert(curve.keys_.begin() + static_cast<long>(insertAt), newKey);
    return true;
}

// Property clip covering the time by its range alone. Unlike FindActivePropertyClip this also
// matches freshly created clips that do not have any curve yet.
TimelinePropertyClip* FindCoveringPropertyClip(TimelinePropertyTrack& track, float time)
{
    TimelinePropertyClip* result = nullptr;
    for (TimelinePropertyClip& clip : track.clips_)
    {
        if (time >= clip.start_ && time <= clip.GetEndTime())
            result = &clip;
    }
    return result;
}

// Whether values of this attribute type can be captured as property keys.
bool IsRecordableType(VariantType type)
{
    switch (type)
    {
    case VAR_FLOAT:
    case VAR_DOUBLE:
    case VAR_BOOL:
    case VAR_INT:
    case VAR_VECTOR2:
    case VAR_VECTOR3:
    case VAR_VECTOR4:
    case VAR_QUATERNION:
    case VAR_COLOR:
    case VAR_INTVECTOR2:
    case VAR_INTVECTOR3:
    case VAR_MATRIX3:
    case VAR_MATRIX3X4:
    case VAR_STRING:
        return true;
    default:
        return false;
    }
}

// Whether the component references at least one material resource through its attributes.
bool HasComponentMaterial(Component& component)
{
    const auto* attributes = component.GetAttributes();
    if (!attributes)
        return false;
    for (const AttributeInfo& info : *attributes)
    {
        Variant value;
        if (info.type_ == VAR_RESOURCEREF)
        {
            component.OnGetAttribute(info, value);
            if (!value.IsEmpty())
            {
                const ResourceRef& ref = value.GetResourceRef();
                if (ref.type_ == Material::GetTypeStatic() && !ref.name_.empty())
                    return true;
            }
        }
        else if (info.type_ == VAR_RESOURCEREFLIST)
        {
            component.OnGetAttribute(info, value);
            const ResourceRefList& list = value.GetResourceRefList();
            if (list.type_ == Material::GetTypeStatic())
            {
                for (const ea::string& name : list.names_)
                {
                    if (!name.empty())
                        return true;
                }
            }
        }
    }
    return false;
}

// Whether the object has at least one attribute of a recordable type.
bool HasRecordableAttribute(Serializable& object)
{
    const auto* attributes = object.GetAttributes();
    if (!attributes)
        return false;
    for (const AttributeInfo& info : *attributes)
    {
        if (IsRecordableType(info.type_))
            return true;
    }
    return false;
}

// Whether any property track can record: there is a bound Serializable target with at least
// one recordable attribute. Recording no longer requires the cursor to be over an existing
// clip: a clip is created at the cursor when the first change is detected.
bool HasRecordableProperty(TimelinePlayer* player, TimelineResource* timeline, float time)
{
    if (!player || !timeline)
        return false;

    // Nested tracks are addressed by their path, so the hierarchy is walked depth-first here and
    // everywhere else that has to see every track of the timeline.
    ea::vector<TrackRow> rows;
    FlattenTracks(timeline, rows);
    for (const TrackRow& row : rows)
    {
        TimelinePropertyTrack* track = AsPropertyTrack(row.track_);
        if (!track || track->IsMuted())
            continue;

        auto* target = dynamic_cast<Serializable*>(ResolveBoundTarget(player, timeline, row.ref_, track));
        if (!target)
            continue;

        // A bound Material records its shader parameters and texture units.
        if (auto* material = dynamic_cast<Material*>(target))
        {
            if (!material->GetShaderParameters().empty() || !material->GetTextures().empty())
                return true;
            continue;
        }

        // A bound node or component also records the materials of its components.
        if (auto* node = dynamic_cast<Node*>(target))
        {
            for (const SharedPtr<Component>& component : node->GetComponents())
            {
                if (component && (HasComponentMaterial(*component) || HasRecordableAttribute(*component)))
                    return true;
            }
        }
        else if (auto* component = dynamic_cast<Component*>(target))
        {
            if (HasComponentMaterial(*component) || HasRecordableAttribute(*component))
                return true;
        }

        const auto* attributes = target->GetAttributes();
        if (!attributes)
            continue;

        for (const AttributeInfo& info : *attributes)
        {
            if (IsRecordableType(info.type_))
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

    ea::vector<TrackRow> rows;
    FlattenTracks(timeline, rows);
    for (const TrackRow& row : rows)
    {
        auto* animationTrack = dynamic_cast<TimelineAnimationTrack*>(row.track_);
        if (!animationTrack || animationTrack->IsMuted())
            continue;

        Object* bound = ResolveBoundTarget(player, timeline, row.ref_, animationTrack);
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
    keepCursorVisible_ = false;
    // The frame ruler and the frame based snapping follow the frame rate stored in the resource.
    fps_ = timeline_ ? timeline_->GetFrameRate() : 60.0f;
    collapsedTracks_.clear();
    rows_.clear();
    // Solo and edit lock are session only view state of the displayed timeline.
    ClearOverlayState();
    // The loop region and the pinned rows are view state of the displayed timeline: the region is
    // described in seconds and must not survive into a resource that may be shorter, while a pinned
    // row of one project means nothing in another. The pin set itself is dropped by
    // ClearOverlayState() above.
    loopPreview_ = false;
    loopStart_ = 0.0f;
    loopEnd_ = 0.0f;
    loopDragMode_ = LoopDragMode::None;
    playbackSpeed_ = 1.0f;
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
    // While the binding is locked, selections must not retarget the window at all.
    if (bindingLocked_)
        return;

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
            if (recordedKeyCount_)
                URHO3D_LOGINFO("Timeline: recording finished, {} key(s) inserted.", recordedKeyCount_);
            recordedKeyCount_ = 0;
        }
        else
        {
            // Recording samples the values edited in the Inspector: the preview must
            // stop driving them, otherwise every frame would be recorded instead.
            StopPreview();
            recording_ = true;
            recordedValues_.clear();
            recordedKeyCount_ = 0;
            LogRecordingSetup(player);
        }
    }

    if (recording_)
    {
        ui::SameLine();
        ui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "REC (%u)", recordedKeyCount_);
        if (!recordingTargets_.empty())
        {
            ui::SameLine();
            ui::TextDisabled("recording: %s", recordingTargets_.c_str());
        }

        // Warn when nothing can be recorded: there is no property track with a valid target.
        if (!HasRecordableProperty(player, timeline_, cursorTime_))
        {
            ui::SameLine();
            ui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f), "nothing to record: add a property track with a bound target");
        }
    }

    // Solo is a preview only override, so the toolbar states which tracks it currently applies to.
    if (!soloedTracks_.empty())
    {
        ui::SameLine();
        ui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "Solo: %u", soloedTracks_.size());
    }

    // Playback and recording are driven through the player of the bound node: explain why the
    // buttons would stay inert.
    if (!player)
    {
        ui::SameLine();
        ui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "bound node has no TimelinePlayer of this timeline");
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

    // The solo overrides are pushed onto the player of the live session every frame: the table is
    // small, and reapplying it makes a player that entered the scene later, or a solo toggled
    // before the first preview started, take effect without any extra entry point.
    if (player && previewState_ != PreviewState::Off)
        ApplySoloOverrides();

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
            // The tab owns the preview clock, so the toolbar multiplier is applied to it here. The
            // player is told about the speed as well, so that a scene update, whenever one runs,
            // advances at the same rate instead of fighting the editor's clock.
            const float timeStep = Clamp(static_cast<float>(now - previewLastTime_), 0.0f, 0.1f) * playbackSpeed_;
            activePlayer->SetPlaybackSpeed(playbackSpeed_);
            cursorTime_ = cursorTime_ + timeStep;
            if (loopPreview_ && loopEnd_ > loopStart_ && cursorTime_ >= loopEnd_)
            {
                // Wrap inside the loop region. The region is editor side only: the wrap mode
                // attribute of the player, which a runtime timeline would read from the scene, is
                // left alone.
                cursorTime_ = loopStart_ + Mod(cursorTime_ - loopStart_, loopEnd_ - loopStart_);
            }
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
        // Closing the preview session also drops the solo overrides: the next session starts from
        // the mute flags stored in the resource, and the solo set of the tab is reapplied if any.
        player->ClearTrackSilences();
    }

    previewPlayer_.Reset();
    previewState_ = PreviewState::Off;
    previewLastTime_ = 0.0;
}

void TimelineViewTab::LogRecordingSetup(TimelinePlayer* player)
{
    URHO3D_LOGINFO("Timeline: recording started at {:.3f}s (frame {:.0f}).", cursorTime_, cursorTime_ * fps_);

    if (!player)
    {
        URHO3D_LOGWARNING("Timeline: the displayed timeline is not bound to the node with a matching "
            "TimelinePlayer component, so nothing can be recorded. Select the node that owns the player "
            "or add a TimelinePlayer component.");
        return;
    }
    if (!timeline_)
        return;

    unsigned numPropertyTracks = 0;
    recordingTargets_.clear();
    ea::vector<TrackRow> rows;
    FlattenTracks(timeline_, rows);
    for (unsigned ti = 0; ti < rows.size(); ++ti)
    {
        TimelinePropertyTrack* track = AsPropertyTrack(rows[ti].track_);
        if (!track)
            continue;
        ++numPropertyTracks;

        Object* bound = ResolveBoundTarget(player, timeline_, rows[ti].ref_, track);
        auto* target = dynamic_cast<Serializable*>(bound);
        if (!bound)
        {
            URHO3D_LOGWARNING("Timeline: property track {} binding '{}' does not resolve to an object; "
                "edits of that track cannot be recorded.", ti, track->GetBindingPath().c_str());
            AppendRecordingTargetHint(ti, "<unresolved>");
        }
        else if (!target)
        {
            URHO3D_LOGWARNING("Timeline: property track {} binds to a {} which has no attributes; "
                "edits of that track cannot be recorded.", ti, bound->GetTypeName().c_str());
            AppendRecordingTargetHint(ti, bound->GetTypeName());
        }
        else
        {
            ea::string description = target->GetTypeName();
            if (auto* node = dynamic_cast<Node*>(bound))
                description = Format("Node '{}'", node->GetName().empty() ? "(unnamed)" : node->GetName().c_str());
            else if (auto* component = dynamic_cast<Component*>(bound))
                description = Format("{} of '{}'", component->GetTypeName().c_str(),
                    component->GetNode() ? component->GetNode()->GetName().c_str() : "(no node)");
            else if (auto* material = dynamic_cast<Material*>(bound))
            {
                const ea::string materialName = GetResourceName(material);
                description = Format("Material '{}'", materialName.empty() ? "(unnamed)" : materialName.c_str());
            }
            URHO3D_LOGINFO("Timeline: property track {} records attributes of the {} bound to '{}'.",
                ti, description.c_str(), track->GetBindingPath().c_str());
            AppendRecordingTargetHint(ti, description);
        }
    }

    if (!numPropertyTracks)
        URHO3D_LOGWARNING("Timeline: the timeline has no property track, so gizmo and Inspector edits are not recorded.");
}

void TimelineViewTab::AppendRecordingTargetHint(unsigned trackIndex, const ea::string& description)
{
    if (!recordingTargets_.empty())
        recordingTargets_ += ", ";
    recordingTargets_ += Format("track {}: {}", trackIndex, description);
}

TimelinePropertyClip* TimelineViewTab::FindOrCreateClipAtCursor(TimelinePropertyTrack& track, float& localTime)
{
    localTime = 0.0f;
    TimelinePropertyClip* clip = FindCoveringPropertyClip(track, cursorTime_);
    if (!clip)
    {
        // No clip covers the cursor: create one when the first change is detected, so nothing
        // the user records is silently dropped. A fresh track records from the timeline start,
        // which puts the pre-change baseline key at frame 0 as expected. If the track already
        // has clips elsewhere, the new one starts at the cursor to avoid overlapping them.
        BeginModify();
        TimelinePropertyClip newClip;
        newClip.start_ = track.clips_.empty() ? 0.0f : Max(cursorTime_, 0.0f);
        newClip.duration_ = Max(Max(timeline_->GetDuration(), cursorTime_ + 1.0f) - newClip.start_, 1.0f);
        track.clips_.push_back(newClip);
        clip = &track.clips_.back();
    }

    localTime = Max(cursorTime_ - clip->start_, 0.0f);
    return clip;
}

void TimelineViewTab::UpdateRecording(TimelinePlayer* player)
{
    if (!player || !timeline_)
        return;

    bool modificationStarted = false;

    // Shared capture-and-key logic for one channel of one track: sample the current value,
    // compare it to the last one, and write a key at the cursor when it changed.
    const auto captureChannel = [&](TimelinePropertyTrack& track, Object& target, const ea::string& channelName,
        VariantType valueType, const Variant& value)
    {
        const ea::string cacheKey = Format("{}:{}", static_cast<const void*>(&target), channelName);
        const auto iter = recordedValues_.find(cacheKey);
        if (iter == recordedValues_.end())
        {
            // The first sample only establishes the baseline value captured when recording
            // started; it never creates keys.
            RecordedChannel channel;
            channel.baseline_ = value;
            channel.last_ = value;
            recordedValues_[cacheKey] = channel;
            return;
        }
        if (iter->second.last_ == value)
            return;

        if (!modificationStarted)
        {
            BeginModify();
            modificationStarted = true;
        }

        float localTime = 0.0f;
        TimelinePropertyClip* clip = FindOrCreateClipAtCursor(track, localTime);

        TimelinePropertyCurve* curve = nullptr;
        for (TimelinePropertyCurve& c : clip->curves_)
        {
            if (c.name_ == channelName)
            {
                curve = &c;
                break;
            }
        }
        if (!curve)
        {
            TimelinePropertyCurve newCurve;
            newCurve.name_ = channelName;
            newCurve.valueType_ = valueType;
            clip->curves_.push_back(newCurve);
            curve = &clip->curves_.back();
        }

        // The Unity-style take: when nothing has been keyed on this curve yet, the
        // baseline at recording start becomes a key at the clip start, so the movement
        // is interpolated from where the object was. If keys already exist, the change
        // is captured only at the cursor.
        if (curve->keys_.empty())
            SetCurveKeyValue(*curve, 0.0f, iter->second.baseline_);
        if (SetCurveKeyValue(*curve, localTime, value))
        {
            recordedKeyCount_++;
            URHO3D_LOGINFO("Timeline: recorded key '{}' of {} at {:.3f}s (clip starts at {:.3f}s).",
                channelName, target.GetTypeName().c_str(), cursorTime_, clip->start_);
        }

        iter->second.last_ = value;
    };

    // Capture the shader parameters and texture units of one material, prefixed so that
    // curves of the same material under different components stay distinct.
    const auto captureMaterial = [&](Material& material, TimelinePropertyTrack& track, const ea::string& channelPrefix)
    {
        for (const auto& [hash, param] : material.GetShaderParameters())
        {
            (void)hash;
            if (IsRecordableType(param.value_.GetType()))
                captureChannel(track, material, Format("{}ShaderParameters/{}", channelPrefix, param.name_),
                    param.value_.GetType(), param.value_);
        }
        for (const auto& [hash, texture] : material.GetTextures())
        {
            (void)hash;
            if (!texture.value_)
                continue;
            const ea::string textureName = GetResourceName(texture.value_);
            if (textureName.empty())
                continue;
            captureChannel(track, material, Format("{}Textures/{}", channelPrefix, texture.name_),
                VAR_RESOURCEREF, Variant(ResourceRef(texture.value_->GetType(), textureName)));
        }
    };

    // Capture all materials referenced by resource attributes of a component (e.g. the
    // "Material" list of a StaticModel), addressed as "Type/Attr[/Index]/...".
    const auto captureComponentMaterials = [&](Component& component, TimelinePropertyTrack& track)
    {
        const auto* componentAttributes = component.GetAttributes();
        if (!componentAttributes)
            return;
        auto* cache = component.GetSubsystem<ResourceCache>();
        for (const AttributeInfo& info : *componentAttributes)
        {
            if (info.type_ == VAR_RESOURCEREF)
            {
                Variant refValue;
                component.OnGetAttribute(info, refValue);
                if (refValue.IsEmpty())
                    continue;
                const ResourceRef& ref = refValue.GetResourceRef();
                if (ref.type_ != Material::GetTypeStatic() || ref.name_.empty())
                    continue;
                if (auto* material = cache->GetResource<Material>(ref.name_))
                    captureMaterial(*material, track, Format("{}/{}/", component.GetTypeName(), info.name_));
            }
            else if (info.type_ == VAR_RESOURCEREFLIST)
            {
                Variant refValue;
                component.OnGetAttribute(info, refValue);
                const ResourceRefList& list = refValue.GetResourceRefList();
                if (list.type_ != Material::GetTypeStatic())
                    continue;
                for (unsigned i = 0; i < list.names_.size(); ++i)
                {
                    if (list.names_[i].empty())
                        continue;
                    if (auto* material = cache->GetResource<Material>(list.names_[i]))
                        captureMaterial(*material, track, Format("{}/{}/{}/", component.GetTypeName(), info.name_, i));
                }
            }
        }
    };

    // Capture the recordable attributes of one component. Node-bound tracks prefix the channel
    // with the component type (and an index when several components share the type) so curves
    // resolve back to the right component; component-bound tracks use the bare attribute name
    // because the component itself is the binding target.
    const auto captureComponentAttributes = [&](Component& component, TimelinePropertyTrack& track,
        const ea::string& channelPrefix)
    {
        const auto* componentAttributes = component.GetAttributes();
        if (!componentAttributes)
            return;
        for (const AttributeInfo& info : *componentAttributes)
        {
            if (!IsRecordableType(info.type_))
                continue;
            Variant value;
            component.OnGetAttribute(info, value);
            if (value.IsEmpty())
                continue;
            captureChannel(track, component, Format("{}{}", channelPrefix, info.name_), info.type_, value);
        }
    };

    // Every property track of the hierarchy is sampled, including the ones nested in a group: their
    // binding is resolved relative to the object their group is bound to.
    ea::vector<TrackRow> rows;
    FlattenTracks(timeline_, rows);
    for (unsigned ti = 0; ti < rows.size(); ++ti)
    {
        TimelinePropertyTrack* track = AsPropertyTrack(rows[ti].track_);
        if (!track || track->IsMuted())
            continue;

        auto* target = dynamic_cast<Serializable*>(ResolveBoundTarget(player, timeline_, rows[ti].ref_, track));
        if (!target)
            continue;

        if (auto* material = dynamic_cast<Material*>(target))
        {
            // Materials have no reflected attributes: capture their shader parameters and
            // texture units, addressed by prefixed channel names.
            captureMaterial(*material, *track, ea::string{});
            continue;
        }

        // A bound node or component also captures the materials of its components, so editing
        // a material in the Inspector records keys without binding the material itself.
        if (auto* node = dynamic_cast<Node*>(target))
        {
            // Count same-type components to index their channels only when ambiguous.
            ea::unordered_map<StringHash, unsigned> typeCounts;
            for (const SharedPtr<Component>& component : node->GetComponents())
            {
                if (component)
                    ++typeCounts[component->GetType()];
            }
            ea::unordered_map<StringHash, unsigned> typeIndices;
            for (const SharedPtr<Component>& component : node->GetComponents())
            {
                if (!component)
                    continue;
                captureComponentMaterials(*component, *track);
                const unsigned count = typeCounts[component->GetType()];
                const unsigned index = typeIndices[component->GetType()]++;
                captureComponentAttributes(*component, *track,
                    count > 1 ? Format("{}/{}/", component->GetTypeName(), index)
                              : Format("{}/", component->GetTypeName()));
            }
        }
        else if (auto* component = dynamic_cast<Component*>(target))
        {
            captureComponentMaterials(*component, *track);
            captureComponentAttributes(*component, *track, ea::string{});
        }

        const auto* attributes = target->GetAttributes();
        if (!attributes)
            continue;

        for (const AttributeInfo& info : *attributes)
        {
            if (!IsRecordableType(info.type_))
                continue;

            Variant value;
            target->OnGetAttribute(info, value);
            captureChannel(*track, *target, info.name_, info.type_, value);
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
    selectedTrack_ = TrackRef{};
    selectedClip_ = M_MAX_UNSIGNED;
    selectedKey_ = M_MAX_UNSIGNED;
    selectedKeyTime_ = -1.0f;
    multiSelectedClips_.Clear();
}

void TimelineViewTab::SelectTrack(const TrackRef& trackRef)
{
    selectedTrack_ = trackRef;
    selectedClip_ = M_MAX_UNSIGNED;
    selectedKey_ = M_MAX_UNSIGNED;
    selectedKeyTime_ = -1.0f;
    multiSelectedClips_.Clear();
}

TimelineClip* TimelineViewTab::FindSelectedClip(const TrackRef& trackRef, unsigned clipIndex)
{
    TimelineTrack* track = TrackAt(timeline_, trackRef);
    return track ? GetClip(track, clipIndex) : nullptr;
}

TimelinePropertyClip* TimelineViewTab::FindSelectedPropertyClip()
{
    auto* track = AsPropertyTrack(TrackAt(timeline_, selectedTrack_));
    if (!track || selectedClip_ >= track->clips_.size())
        return nullptr;
    return &track->clips_[selectedClip_];
}

void TimelineViewTab::RebuildRows()
{
    // View state belongs to tracks that exist: undo of a track removal must not make the
    // indices of the rebuilt lists address unrelated groups.
    PruneStaleRefs(collapsedTracks_, timeline_);
    PruneStaleRefs(soloedTracks_, timeline_);
    PruneStaleRefs(lockedTracks_, timeline_);
    PruneStaleRefs(pinnedTracks_, timeline_);
    BuildTrackRows(timeline_, collapsedTracks_, rows_);
}

int TimelineViewTab::RowIndexOf(const TrackRef& trackRef) const
{
    for (unsigned i = 0; i < rows_.size(); ++i)
    {
        if (rows_[i].ref_ == trackRef)
            return static_cast<int>(i);
    }
    return -1;
}

void TimelineViewTab::AddTrackOfType(const ea::string& typeName, const TrackRef& parent)
{
    SharedPtr<TimelineTrack> track = CreateTrackByTypeName(context_, typeName);
    if (!track)
        return;

    BeginModify();
    // Appending into the destination list: top level when the parent reference is invalid.
    TrackRef ref;
    if (!AddTrackTo(timeline_, track.Get(), parent, M_MAX_UNSIGNED, ref))
        return;
    RebuildRows();
    SelectTrack(ref);
}

void TimelineViewTab::RemoveTrackAt(const TrackRef& trackRef)
{
    TimelineTrack* track = TrackAt(timeline_, trackRef);
    if (!track)
        return;

    BeginModify();
    // A group is unwrapped instead of deleted: its tracks are lifted to the position of the group,
    // so removing the organizer of a hierarchy never removes the organized content.
    if (!UnwrapGroup(timeline_, track))
        RemoveTrackDeep(timeline_, track);
    ClearSelection();
    RebuildRows();
}

bool TimelineViewTab::IsTrackSoloed(const TrackRef& trackRef) const
{
    return ContainsRef(soloedTracks_, trackRef);
}

void TimelineViewTab::ToggleTrackSolo(const TrackRef& trackRef)
{
    if (!TrackAt(timeline_, trackRef))
        return;
    ToggleRef(soloedTracks_, trackRef);
    ApplySoloOverrides();
}

bool TimelineViewTab::IsTrackLocked(const TrackRef& trackRef) const
{
    return ContainsRef(lockedTracks_, trackRef);
}

void TimelineViewTab::ToggleTrackLocked(const TrackRef& trackRef)
{
    if (!TrackAt(timeline_, trackRef))
        return;
    if (ToggleRef(lockedTracks_, trackRef))
    {
        // Locking the track the sequencer currently points at would leave the selection panel
        // editing a track the user can no longer interact with.
        if (selectedTrack_ == trackRef)
            ClearSelection();
    }
}

bool TimelineViewTab::IsTrackPinned(const TrackRef& trackRef) const
{
    return ContainsRef(pinnedTracks_, trackRef);
}

void TimelineViewTab::ToggleTrackPinned(const TrackRef& trackRef)
{
    if (!TrackAt(timeline_, trackRef))
        return;
    ToggleRef(pinnedTracks_, trackRef);
}

void TimelineViewTab::ApplySoloOverrides()
{
    TimelinePlayer* player = GetContextPlayer();
    if (!player)
        return;

    // Solo is exclusive the way Unity does it: as soon as one track is soloed, every other track of
    // the hierarchy is silenced. The overrides live on the player and are never serialized, so the
    // saved timeline is unaffected.
    if (soloedTracks_.empty())
    {
        player->ClearTrackSilences();
        return;
    }

    ea::vector<TrackRow> rows;
    FlattenTracks(timeline_, rows);
    for (const TrackRow& row : rows)
        player->SetTrackSilenced(row.track_, !ContainsRef(soloedTracks_, row.ref_));
}

void TimelineViewTab::ClearOverlayState()
{
    soloedTracks_.clear();
    lockedTracks_.clear();
    pinnedTracks_.clear();
    if (TimelinePlayer* player = GetContextPlayer())
        player->ClearTrackSilences();
}

void TimelineViewTab::CopySelectedClips()
{
    if (!timeline_)
        return;

    TimelineTrack* src = TrackAt(timeline_, selectedTrack_);
    if (!src)
        return;

    // Collect the source clip indices: all multi-selected clips on the current track, otherwise
    // just the single selected clip.
    ea::vector<unsigned> indices;
    for (const ClipKey& key : multiSelectedClips_.Keys())
    {
        if (key.track_ == selectedTrack_)
            indices.push_back(key.clip_);
    }
    if (indices.empty() && selectedClip_ != M_MAX_UNSIGNED)
        indices.push_back(selectedClip_);
    if (indices.empty())
        return;

    clipboardTrackType_ = src->GetTrackTypeName();
    clipboardTrack_ = CreateTrackByTypeName(context_, clipboardTrackType_);
    if (!clipboardTrack_)
        return;
    for (unsigned index : indices)
        CopyClipToTrackOfType(clipboardTrack_.Get(), src, index);
}

void TimelineViewTab::PasteClips(float pasteTime)
{
    if (!clipboardTrack_ || !timeline_)
        return;

    TimelineTrack* dst = TrackAt(timeline_, selectedTrack_);
    if (!dst)
        return;
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
    multiSelectedClips_.Clear();
    for (unsigned index : pasted)
        multiSelectedClips_.Insert(selectedTrack_, index);
}

void TimelineViewTab::ValidateSelection()
{
    if (!timeline_)
    {
        ClearSelection();
        return;
    }

    TimelineTrack* track = TrackAt(timeline_, selectedTrack_);
    if (!track)
    {
        ClearSelection();
        return;
    }

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
    auto& keys = multiSelectedClips_.keys_;
    for (auto iter = keys.begin(); iter != keys.end();)
    {
        TimelineTrack* clipTrack = TrackAt(timeline_, iter->track_);
        const bool valid = clipTrack && iter->clip_ < GetNumClips(clipTrack);
        iter = valid ? ea::next(iter) : keys.erase(iter);
    }
    if (keys.size() <= 1)
        keys.clear();
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
    // The visible rows of the track hierarchy drive both layout and hit-testing of the body.
    RebuildRows();

    // Refresh DPI-scaled layout before anything is measured or drawn, so rendering and
    // hit-testing agree and the widget keeps a constant physical size across displays.
    float dpiScale = 1.0f;
    if (auto* renderDevice = context_->GetSubsystem<RenderDevice>())
        dpiScale = Max(renderDevice->GetDpiScale(), 1.0f);
    ApplyLayoutScale(dpiScale);

    // The bottom properties panel keeps its user-chosen height; the splitter below the
    // sequencer resizes it so several property rows are visible at once.
    RenderToolbar();

    const float splitterHeight = 8.0f;
    // The marker strip is always there, the list below it only while the panel is expanded. The
    // sequencer and the properties panel are sized from what is left, so the strip must be
    // subtracted from both budgets, otherwise the tab overflows its own height.
    const float markersHeight = MarkersStripHeight + (markersPanelOpen_ ? MarkersPanelHeight : 0.0f) + 8.0f;
    const ImVec2 avail = ui::GetContentRegionAvail();
    const float maxPanelHeight = Max(avail.y - 140.0f - splitterHeight - markersHeight, 60.0f);
    selectionPanelHeight_ = Clamp(selectionPanelHeight_, 60.0f, maxPanelHeight);
    const float sequencerHeight = Max(avail.y - selectionPanelHeight_ - splitterHeight - markersHeight - ZoomBarHeight,
        120.0f);
    ui::BeginChild("timeline_sequencer", ImVec2(avail.x, sequencerHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    RenderSequencer();
    ui::EndChild();

    // Unity-style zoom bar between the sequencer and the splitter.
    RenderZoomSlider();

    // Drag handle between the sequencer and the properties panel.
    ImGuiIO& io = ui::GetIO();
    ui::InvisibleButton("timeline_panel_splitter", ImVec2(avail.x, splitterHeight));
    if (ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        ui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ui::IsItemClicked(ImGuiMouseButton_Left))
        draggingPanelSplitter_ = true;
    if (draggingPanelSplitter_)
    {
        if (!io.MouseDown[ImGuiMouseButton_Left])
            draggingPanelSplitter_ = false;
        else
            selectionPanelHeight_ = Clamp(selectionPanelHeight_ - io.MouseDelta.y, 60.0f, maxPanelHeight);
    }
    const ImVec2 splitMin = ui::GetItemRectMin();
    const ImVec2 splitMax = ui::GetItemRectMax();
    const float gripY = (splitMin.y + splitMax.y) * 0.5f;
    ui::GetWindowDrawList()->AddLine(ImVec2(splitMin.x + 4.0f, gripY), ImVec2(splitMax.x - 4.0f, gripY),
        draggingPanelSplitter_ ? IM_COL32(216, 130, 0, 255) : IM_COL32(120, 120, 120, 255), 2.0f);

    // Marker list between the sequencer and the properties panel of the selection.
    RenderMarkersPanel();

    RenderSelectionPanel();

    // Playback speed entry of the clip context menu. It is a window rather than a popup because it
    // is opened from a menu, whose id scope a popup would be registered under.
    RenderClipSpeedDialog();

    // The curve editor is a standalone window: the properties panel is too narrow for key and
    // tangent work, and with docking the window can be stacked behind the timeline tab. It
    // follows the selected property clip and closes when that selection is gone.
    if (curveEditor_.IsOpen())
    {
        if (TimelinePropertyClip* clip = FindSelectedPropertyClip())
            curveEditor_.Render(*clip, *this);
        else
            curveEditor_.Close();
    }

    // The dope sheet shows the whole hierarchy, so unlike the curve editor it does not depend on a
    // selection: it is fed the rows the sequencer is displaying right now.
    if (dopeSheet_.IsOpen())
        dopeSheet_.Render(*this, rows_, *timeline_);
}

void TimelineViewTab::RenderToolbar()
{
    // Toolbar may be rendered by the project before any resource is loaded
    if (!timeline_)
        return;

    // Lock toggle drawn as an icon: lock.png when locked, unlock.png when following
    // the selection. Loaded lazily from the EditorData resource root.
    if (!lockIcon_)
        lockIcon_ = GetSubsystem<ResourceCache>()->GetResource<Texture2D>("Textures/lock.png");
    if (!unlockIcon_)
        unlockIcon_ = GetSubsystem<ResourceCache>()->GetResource<Texture2D>("Textures/unlock.png");

    // Push/Pop must pair by the state at draw time: clicking flips bindingLocked_ between
    // the two checks, so remember the state that decided the Push.
    const bool wasLocked = bindingLocked_;
    if (wasLocked)
        ui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.35f, 0.15f, 1.0f));
    Texture2D* lockIcon = bindingLocked_ ? lockIcon_.Get() : unlockIcon_.Get();
    // Widgets::ImageButton has no button frame, which would make it shorter than the
    // other toolbar buttons: draw a regular Button and overlay the centered icon.
    const bool clicked = lockIcon
        ? [&]
        {
            const bool pressed = ui::Button("     ");
            const ImVec2 pos = ui::GetItemRectMin();
            const ImVec2 size = ui::GetItemRectSize();
            const float iconSize = Min(size.y - 2.0f, size.x - 4.0f);
            ui::GetWindowDrawList()->AddImage(ToImTextureID(lockIcon),
                ImVec2(pos.x + (size.x - iconSize) * 0.5f, pos.y + (size.y - iconSize) * 0.5f),
                ImVec2(pos.x + (size.x + iconSize) * 0.5f, pos.y + (size.y + iconSize) * 0.5f));
            return pressed;
        }()
        : ui::Button(bindingLocked_ ? "Binding: Locked" : "Binding: Auto");
    if (clicked)
        bindingLocked_ = !bindingLocked_;
    if (wasLocked)
        ui::PopStyleColor();
    if (ui::IsItemHovered())
        ui::SetTooltip("Lock the bound context node.\n"
            "While locked, selecting other nodes in the scene will not switch\n"
            "the timeline shown here. Click again to unlock.");

    ui::SameLine();
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
                AddTrackOfType(typeName, TrackRef{});
        }

        // Groups offer the same list as a submenu, which creates the track inside them instead of
        // nesting it by dragging. Nested groups list their own children as well, so a whole
        // hierarchy can be built from the menu. The entries are snapshotted because adding a track
        // rebuilds the row list the menu is iterating over.
        struct GroupEntry
        {
            TrackRef ref_;
            ea::string label_;
        };
        ea::vector<GroupEntry> groups;
        for (const TrackRow& row : rows_)
        {
            if (!row.isGroup_ || !row.track_)
                continue;
            const ea::string name = row.track_->GetName().empty()
                ? row.track_->GetTrackTypeName()
                : row.track_->GetName();
            groups.push_back(GroupEntry{row.ref_, Format("Inside '%s' (%s)", name.c_str(), row.ref_.ToString().c_str())});
        }
        if (!groups.empty())
        {
            ui::Separator();
            for (const GroupEntry& group : groups)
            {
                if (ui::BeginMenu(group.label_.c_str()))
                {
                    for (const char* typeName : trackTypeNames)
                    {
                        if (ui::Selectable(Format("Add {} Track", typeName).c_str()))
                            AddTrackOfType(typeName, group.ref_);
                    }
                    ui::EndMenu();
                }
            }
        }
        ui::EndPopup();
    }
    ui::SameLine();

    // Unity's sequencer has a scissors button on the toolbar: it cuts every selected clip at the
    // cursor, wherever the cursor happens to be.
    if (ui::Button("Split"))
        SplitSelectedClips(cursorTime_);
    if (ui::IsItemHovered())
        ui::SetTooltip("Cut the selected clips at the playhead.\n"
            "Clips that do not span the playhead are left unchanged.\n"
            "Ctrl+D duplicates the selection, Alt+drag copies a clip.");

    ui::SameLine();

    // Unity keeps the dope sheet as a dockable window of its own; the toolbar only toggles it and
    // the window remembers its own view range and selection while it is hidden.
    if (ui::Button("Dope Sheet"))
    {
        if (dopeSheet_.IsOpen())
            dopeSheet_.Close();
        else
            dopeSheet_.Open();
    }
    if (ui::IsItemHovered())
        ui::SetTooltip("Time over rows overview of the whole hierarchy.\n"
            "Keys are squares, clips are blocks.\n"
            "Click the ruler to select a key row, F fits the view.");

    ui::SameLine();

    // Snapping. Unity puts a single magnet on the toolbar; the candidate sets it may grab are
    // offered in its dropdown, so the common case stays one click. The button is highlighted while
    // any set is on, and the style stack is paired by the state before the click, because clicking
    // the popup open must not flip the highlight of the button that is still being drawn.
    const bool anySnap = snapToClips_ || snapToKeys_;
    if (anySnap)
        ui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.42f, 0.60f, 1.0f));
    if (ui::Button(anySnap ? "Snap  *" : "Snap  -"))
        ui::OpenPopup("SnapPopup");
    if (anySnap)
        ui::PopStyleColor();
    if (ui::IsItemHovered())
        ui::SetTooltip("Snap dragged clips, keys and markers to the grid and to the points listed\n"
            "here. The grid is the ruler unit: 0.05 s in the seconds mode, one frame in the\n"
            "frames mode. Hold Ctrl while dragging to suppress snapping entirely.");

    if (ui::BeginPopup("SnapPopup"))
    {
        bool clips = snapToClips_;
        if (ui::Checkbox("Clip edges", &clips))
            snapToClips_ = clips;
        bool keys = snapToKeys_;
        if (ui::Checkbox("Keys and markers", &keys))
            snapToKeys_ = keys;
        ui::Separator();
        ui::TextDisabled("Tolerance: 8 px");
        ui::EndPopup();
    }

    ui::SameLine();

    {
        // Ruler unit. The mode is display only: it drives the ruler labels, the cursor readout and
        // the grid drags snap to. In the frames mode the frame band above the ruler is dropped,
        // because the ruler itself already counts frames.
        ui::SetNextItemWidth(90.0f);
        ui::Combo("##timeUnit", &timeDisplayMode_, "Seconds\0Frames\0");
    }
    ui::SameLine();

    {
        int durationMode = timeline_->GetDurationMode() == TimelineDurationMode::Fixed ? 0 : 1;
        // Wide enough for "Content" plus the dropdown arrow, also at higher font scales.
        ui::SetNextItemWidth(120.0f);
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
    if (ui::Button("Fit"))
    {
        const float trackAreaWidth = Max(ui::GetContentRegionAvail().x - HeaderWidth - 20.0f, 100.0f);
        pixelsPerSecond_ = Clamp(trackAreaWidth / Max(timeline_->GetDuration(), 0.1f), MinPixelsPerSecond, MaxPixelsPerSecond);
        viewStartTime_ = 0.0f;
    }
    if (ui::IsItemHovered())
        ui::SetTooltip("Show the whole timeline.\nShift+F frames the selection or the loop region.");

    ui::SameLine();

    // Loop preview. The region lives on the lane above the ruler and is editor side only: nothing
    // of it is written to the resource or to the player's wrap mode attribute.
    if (ui::Checkbox("Loop", &loopPreview_))
    {
        if (loopPreview_ && loopEnd_ <= loopStart_)
        {
            loopStart_ = 0.0f;
            loopEnd_ = timeline_->GetDuration();
        }
    }
    if (ui::IsItemHovered())
        ui::SetTooltip("Wrap the play head inside the loop region while playing.\n"
            "Drag the lane above the ruler to define the region, right click it for shortcuts.");

    ui::SameLine();
    {
        ui::SetNextItemWidth(80.0f);
        if (ui::DragFloat("Speed", &playbackSpeed_, 0.05f, 0.05f, 8.0f, "%.2fx"))
        {
            if (TimelinePlayer* activePlayer = previewPlayer_.Get())
                activePlayer->SetPlaybackSpeed(playbackSpeed_);
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Preview playback speed. Editor only: the resource and a runtime\n"
                "playback of the same timeline are not affected.");
    }

    ui::SameLine();
    ui::TextDisabled("%s", FormatDisplayTime(cursorTime_).c_str());
    ui::SameLine();
    ui::TextDisabled("wheel on ruler: zoom | middle-drag: pan | Ctrl: no snap | drag lane: loop | "
        "Home/End , . [ ] : cursor | S: add key | Shift+F: fit selection");

    if (!bindingError_.empty() && ui::GetTime() - bindingErrorTime_ < 4.0)
    {
        ui::SameLine();
        ui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", bindingError_.c_str());
    }
}

void TimelineViewTab::RenderZoomSlider()
{
    const float duration = Max(timeline_->GetDuration(), 0.1f);

    // The zoom bar maps the whole timeline across the full window width. The visible range is
    // computed from the canvas width exactly like the sequencer maps time to pixels. The bar is
    // full when the view covers twice the duration (half the Fit zoom), so zooming out from Fit
    // still leaves room to see all clips with margins around them.
    const float trackWidth = Max(ui::GetContentRegionAvail().x - HeaderWidth, 100.0f);
    const float maxVisibleDuration = duration * 2.0f;
    const float visibleDuration = Clamp(trackWidth / pixelsPerSecond_, 0.0f, maxVisibleDuration);
    const float maxViewStart = Max(duration - visibleDuration, 0.0f);
    viewStartTime_ = Clamp(viewStartTime_, 0.0f, maxViewStart);

    const ImVec2 barPos = ui::GetCursorScreenPos();
    const float barWidth = Max(ui::GetContentRegionAvail().x, 100.0f);
    const ImRect barRect{barPos, ImVec2(barPos.x + barWidth, barPos.y + ZoomBarHeight)};
    const float cornerRadius = ZoomBarHeight * 0.5f;

    // Current view window inside the bar. The window position is mapped on the content range
    // (the whole bar is the timeline duration), while its width is mapped so that a full bar
    // means twice the duration (half the Fit zoom): zooming out from Fit still leaves room.
    const float winX0 = barPos.x + viewStartTime_ / duration * barWidth;
    const float winX1 = Min(winX0 + visibleDuration / maxVisibleDuration * barWidth, barRect.Max.x);

    ImGuiIO& io = ui::GetIO();
    ui::InvisibleButton("timeline_zoom_slider", ImVec2(barWidth, ZoomBarHeight));
    const bool hovered = ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const float mouseX = io.MousePos.x;

    constexpr float kHandleWidth = 8.0f;
    const bool overLeftHandle = hovered && mouseX >= winX0 && mouseX <= winX0 + kHandleWidth;
    const bool overRightHandle = hovered && mouseX >= winX1 - kHandleWidth && mouseX <= winX1;
    const bool overWindow = hovered && mouseX > winX0 + kHandleWidth && mouseX < winX1 - kHandleWidth;

    if (zoomDragMode_ == ZoomDragMode::None)
    {
        if (overLeftHandle || overRightHandle)
            ui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        else if (overWindow)
            ui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (ui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (overLeftHandle)
                zoomDragMode_ = ZoomDragMode::LeftEdge;
            else if (overRightHandle)
                zoomDragMode_ = ZoomDragMode::RightEdge;
            else if (overWindow)
                zoomDragMode_ = ZoomDragMode::Pan;
            else
            {
                // Clicking outside the window jumps the view there.
                const float clickTime = (mouseX - barPos.x) / barWidth * duration;
                viewStartTime_ = Clamp(clickTime - visibleDuration * 0.5f, 0.0f, maxViewStart);
            }
        }
    }
    else
    {
        if (!io.MouseDown[ImGuiMouseButton_Left])
        {
            zoomDragMode_ = ZoomDragMode::None;
        }
        else
        {
            const float viewEndTime = viewStartTime_ + visibleDuration;
            if (zoomDragMode_ == ZoomDragMode::Pan)
            {
                viewStartTime_ = Clamp(viewStartTime_ + io.MouseDelta.x / barWidth * duration, 0.0f, maxViewStart);
            }
            else if (zoomDragMode_ == ZoomDragMode::LeftEdge)
            {
                // Keep the right edge where it is, so the view is anchored to it while zooming.
                const float newStart = Clamp((mouseX - barPos.x) / barWidth * duration, 0.0f, viewEndTime - SnapStep);
                const float newVisible = Max(viewEndTime - newStart, SnapStep);
                pixelsPerSecond_ = Clamp(trackWidth / newVisible, MinPixelsPerSecond, MaxPixelsPerSecond);
                viewStartTime_ = newStart;
            }
            else if (zoomDragMode_ == ZoomDragMode::RightEdge)
            {
                // Keep the left edge where it is, so the view is anchored to it while zooming.
                const float newEnd = viewStartTime_ + (mouseX - winX0) / barWidth * maxVisibleDuration;
                const float clampedEnd = Clamp(newEnd, viewStartTime_ + SnapStep, maxVisibleDuration);
                const float newVisible = Max(clampedEnd - viewStartTime_, SnapStep);
                pixelsPerSecond_ = Clamp(trackWidth / newVisible, MinPixelsPerSecond, MaxPixelsPerSecond);
            }
        }
    }

    // Draw the track, the view window and its edge handles.
    ImDrawList* drawList = ui::GetWindowDrawList();
    drawList->AddRectFilled(barRect.Min, barRect.Max, IM_COL32(45, 45, 52, 255), cornerRadius);
    const ImRect winRect{ImVec2(winX0, barPos.y + 2.0f), ImVec2(winX1, barPos.y + ZoomBarHeight - 2.0f)};
    const ImU32 winColor = zoomDragMode_ != ZoomDragMode::None || hovered
        ? IM_COL32(110, 130, 165, 200)
        : IM_COL32(90, 105, 135, 160);
    const float winCornerRadius = Max(ZoomBarHeight * 0.5f - 2.0f, 0.5f);
    drawList->AddRectFilled(winRect.Min, winRect.Max, winColor, winCornerRadius);
    drawList->AddRect(winRect.Min, winRect.Max, IM_COL32(170, 185, 210, 255), winCornerRadius);
    drawList->AddLine(ImVec2(winX0 + 1.0f, barPos.y + 3.0f), ImVec2(winX0 + 1.0f, barPos.y + ZoomBarHeight - 3.0f),
        IM_COL32(255, 255, 255, 230), 1.5f);
    drawList->AddLine(ImVec2(winX1 - 1.0f, barPos.y + 3.0f), ImVec2(winX1 - 1.0f, barPos.y + ZoomBarHeight - 3.0f),
        IM_COL32(255, 255, 255, 230), 1.5f);

    if (ui::IsItemHovered())
        ui::SetTooltip("Drag the middle to pan, drag the ends to zoom, click to jump");
}

void TimelineViewTab::RenderSequencer()
{
    const float duration = Max(timeline_->GetDuration(), 0.1f);

    // Choose ruler step so that labeled ticks are reasonably spaced. In the frames mode the steps
    // are whole frame counts, which keeps the labels free of fractions.
    float majorStep = SnapStep;
    float minorStep = SnapStep;
    ChooseRulerSteps(pixelsPerSecond_, fps_, TimeUnit(), MinRulerStepPx, 6.0f, majorStep, minorStep);

    const ImVec2 rulerPos = ui::GetCursorScreenPos();
    const float trackX0 = rulerPos.x + HeaderWidth;
    const float trackX1 = rulerPos.x + ui::GetContentRegionAvail().x;

    // Follow the play head so that playback stays visible without manual panning. The same scroll
    // keeps a cursor moved by a keyboard step or a marker jump in view, which is why the request is
    // consumed here rather than where the time was set: this is where the canvas geometry is known.
    // The ruler and the body share the same view offset, so pan before drawing either.
    if (previewState_ == PreviewState::Playing || keepCursorVisible_)
    {
        keepCursorVisible_ = false;
        const float rightMargin = 60.0f;
        const float cursorX = TimeToX(cursorTime_, trackX0);
        if (cursorX > trackX1 - rightMargin)
            viewStartTime_ = Max(cursorTime_ - (trackX1 - rightMargin - trackX0) / pixelsPerSecond_, 0.0f);
        else if (cursorX < trackX0)
            viewStartTime_ = Max(cursorTime_ - 10.0f / pixelsPerSecond_, 0.0f);
    }

    // Loop region lane. It owns a drag of its own, so scrubbing and region editing never collide.
    RenderLoopLane(duration, trackX0, trackX1);
    ui::Dummy(ImVec2(0.0f, LoopLaneHeight));

    if (TimeUnit() == TimelineTimeUnit::Seconds)
    {
        RenderFrameRuler(duration, trackX0, trackX1);
        // Push the layout down past the frame band so the time ruler draws beneath it, then render.
        ui::Dummy(ImVec2(0.0f, FrameRulerHeight));
    }
    RenderRuler(duration, majorStep, minorStep, trackX0, trackX1);

    ui::BeginChild("timeline_body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, 0);
    RenderBody(duration, majorStep, minorStep);
    ui::EndChild();

    RenderLoopContextMenu();
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
            drawList->AddText(ImVec2(x + 3.0f, rulerPos.y + RulerHeight * 0.5f - 6.0f), ColorTickLabel,
                FormatDisplayTime(time).c_str());
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
    // Right clicking the ruler opens the loop menu at the time under the cursor: the ruler is where
    // a user reads and picks times, and a right click there had no meaning before the region.
    if (rulerRect.Contains(io.MousePos) && ui::IsWindowHovered() && ui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        popupTime_ = Max(XToTime(io.MousePos.x, trackX0), 0.0f);
        ui::OpenPopup("LoopContextMenu");
    }
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
            drawList->AddText(ImVec2(cursorX + 8.0f, rulerPos.y + 2.0f), ColorTickLabel, FormatDisplayTime(cursorTime_).c_str());
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

// The loop lane is a strip of its own rather than part of the ruler: a drag over the ruler scrubs
// the play head, and the two gestures must never compete for the same pixels. Dragging empty lane
// creates a region, dragging a handle resizes one and dragging the region moves it whole.
void TimelineViewTab::RenderLoopLane(float duration, float trackX0, float trackX1)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    const ImVec2 pos = ui::GetCursorScreenPos();
    const float y0 = pos.y;
    const float y1 = pos.y + LoopLaneHeight;
    const ImRect laneRect(ImVec2(trackX0, y0), ImVec2(trackX1, y1));

    drawList->AddRectFilled(ImVec2(pos.x, y0), ImVec2(trackX1, y1), ColorLoopLaneBg);
    const float textY = y0 + Max((LoopLaneHeight - ui::GetTextLineHeight()) * 0.5f, 0.0f);
    drawList->AddText(ImVec2(pos.x + 10.0f, textY), loopPreview_ ? ColorTickLabel : ColorHeaderTypeText, "Loop");

    const bool hasRegion = loopEnd_ > loopStart_;
    const float regionX0 = TimeToX(loopStart_, trackX0);
    const float regionX1 = TimeToX(loopEnd_, trackX0);
    if (hasRegion)
    {
        const ImU32 fill = loopPreview_ ? ColorLoopRegion : WithAlpha(ColorLoopRegion, 30);
        const ImU32 border = loopPreview_ ? ColorLoopBorder : WithAlpha(ColorLoopBorder, 110);
        drawList->AddRectFilled(ImVec2(regionX0, y0 + 1.0f), ImVec2(regionX1, y1 - 1.0f), fill);
        drawList->AddRect(ImVec2(regionX0, y0 + 1.0f), ImVec2(regionX1, y1 - 1.0f), border);
        drawList->AddRectFilled(ImVec2(regionX0 - 2.0f, y0), ImVec2(regionX0 + 2.0f, y1), ColorLoopHandle);
        drawList->AddRectFilled(ImVec2(regionX1 - 2.0f, y0), ImVec2(regionX1 + 2.0f, y1), ColorLoopHandle);
    }

    const bool hovered = ui::IsWindowHovered() && laneRect.Contains(io.MousePos);
    const float mouseTime = Max(XToTime(io.MousePos.x, trackX0), 0.0f);
    const float grabPx = Max(4.0f, EdgeGrabWidth * 0.5f);
    const bool overStart = hasRegion && Abs(io.MousePos.x - regionX0) <= grabPx;
    const bool overEnd = hasRegion && Abs(io.MousePos.x - regionX1) <= grabPx;
    const bool overRegion = hasRegion && io.MousePos.x > regionX0 && io.MousePos.x < regionX1;

    if (loopDragMode_ == LoopDragMode::None)
    {
        if (hovered)
        {
            if (overStart || overEnd)
                ui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            else if (overRegion)
                ui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (ui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                loopDragOrigStart_ = loopStart_;
                loopDragOrigEnd_ = loopEnd_;
                loopDragGrab_ = mouseTime;
                if (overStart)
                    loopDragMode_ = LoopDragMode::Start;
                else if (overEnd)
                    loopDragMode_ = LoopDragMode::End;
                else if (overRegion)
                    loopDragMode_ = LoopDragMode::Region;
                else
                {
                    // A drag that starts on empty lane defines a new region from its first two
                    // samples, so a plain click there leaves a zero length region behind.
                    loopDragMode_ = LoopDragMode::Create;
                    loopStart_ = mouseTime;
                    loopEnd_ = mouseTime;
                }
            }
            else if (ui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                popupTime_ = mouseTime;
                ui::OpenPopup("LoopContextMenu");
            }
        }

        if (hovered && loopDragMode_ == LoopDragMode::None)
        {
            if (hasRegion)
                ui::SetTooltip("Loop region %s - %s\n%s\nDrag the ends to resize, the body to move,\nempty space to define a new region.",
                    FormatDisplayTime(loopStart_).c_str(), FormatDisplayTime(loopEnd_).c_str(),
                    loopPreview_ ? "Preview loops here." : "Preview plays through (enable Loop on the toolbar).");
            else
                ui::SetTooltip("Drag here to define the loop preview region.");
        }
    }
    else
    {
        if (!io.MouseDown[ImGuiMouseButton_Left])
        {
            loopDragMode_ = LoopDragMode::None;
        }
        else
        {
            const float time = SnapTime(mouseTime, !io.KeyCtrl);
            switch (loopDragMode_)
            {
            case LoopDragMode::Create:
            {
                const float a = Max(loopStart_, 0.0f);
                loopStart_ = Min(a, time);
                loopEnd_ = Max(a, time);
                break;
            }
            case LoopDragMode::Start:
                loopStart_ = Clamp(time, 0.0f, loopEnd_ - MinClipDuration);
                break;
            case LoopDragMode::End:
                loopEnd_ = Max(time, loopStart_ + MinClipDuration);
                break;
            case LoopDragMode::Region:
            {
                const float width = loopDragOrigEnd_ - loopDragOrigStart_;
                const float newStart = Clamp(SnapTime(mouseTime - loopDragGrab_, !io.KeyCtrl), 0.0f, Max(duration - width, 0.0f));
                loopStart_ = newStart;
                loopEnd_ = newStart + width;
                break;
            }
            default:
                break;
            }
            if (loopEnd_ > duration)
                loopEnd_ = duration;
        }
    }

    // The play head crosses the lane as well, so that the region and the cursor can be compared
    // without looking at the ruler below.
    const float cursorX = TimeToX(cursorTime_, trackX0);
    if (cursorX >= trackX0 && cursorX <= trackX1)
        drawList->AddLine(ImVec2(cursorX, y0), ImVec2(cursorX, y1), ColorCursor, 2.0f);
}

// Loop region menu. Both entry points address the play head time under the cursor: the lane is the
// widget that owns the region, the ruler is where a user looks for a time.
void TimelineViewTab::RenderLoopContextMenu()
{
    if (!ui::BeginPopup("LoopContextMenu"))
        return;

    const float duration = Max(timeline_->GetDuration(), 0.1f);
    ui::TextDisabled("%s", FormatDisplayTime(popupTime_).c_str());

    // The first edge of a region that does not exist yet also establishes the other one, so a
    // single menu click already leaves something usable instead of a zero length region.
    if (ui::Selectable("Set Loop Start"))
    {
        if (loopEnd_ <= popupTime_)
            loopEnd_ = Min(popupTime_ + Max(duration * 0.25f, SnapStep), duration);
        loopStart_ = Clamp(popupTime_, 0.0f, Max(loopEnd_ - MinClipDuration, 0.0f));
    }
    if (ui::Selectable("Set Loop End"))
    {
        if (loopStart_ >= popupTime_)
            loopStart_ = Max(popupTime_ - Max(duration * 0.25f, SnapStep), 0.0f);
        loopEnd_ = Max(popupTime_, loopStart_ + MinClipDuration);
    }
    ui::Separator();
    if (ui::Selectable(loopPreview_ ? "Disable Loop Preview" : "Enable Loop Preview"))
    {
        loopPreview_ = !loopPreview_;
        if (loopPreview_ && loopEnd_ <= loopStart_)
        {
            loopStart_ = 0.0f;
            loopEnd_ = duration;
        }
    }
    if (ui::MenuItem("Fit View to Loop", nullptr, false, loopEnd_ > loopStart_))
    {
        const float width = Max(loopEnd_ - loopStart_, 0.1f);
        const float trackAreaWidth = Max(ui::GetContentRegionAvail().x - HeaderWidth - 20.0f, 100.0f);
        pixelsPerSecond_ = Clamp(trackAreaWidth / width, MinPixelsPerSecond, MaxPixelsPerSecond);
        viewStartTime_ = loopStart_;
    }
    if (ui::MenuItem("Clear Loop", nullptr, false, loopEnd_ > loopStart_))
    {
        loopPreview_ = false;
        loopStart_ = 0.0f;
        loopEnd_ = 0.0f;
    }
    ui::EndPopup();
}

void TimelineViewTab::RenderBody(float duration, float majorStep, float minorStep)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();

    const ImVec2 canvasPos = ui::GetCursorScreenPos();
    const ImVec2 windowPos = ui::GetWindowPos();
    const ImVec2 windowSize = ui::GetWindowSize();
    const ImRect visibleRect(windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y));
    const float trackX0 = canvasPos.x + HeaderWidth;
    const float trackX1 = windowPos.x + windowSize.x;

    // Row layout of the visible hierarchy. One entry per row of rows_, which RebuildRows()
    // refreshed for the current track lists and collapse state, so layout and hit-testing agree.
    // Pinned rows are lifted out of the scrolling extent into a sticky band at the top of the body:
    // the rows below them scroll past the band instead of under it, which keeps a pinned track
    // reachable no matter how far the hierarchy has been scrolled.
    struct RowLayout
    {
        unsigned rowIndex_{};
        float y0_{};
        float y1_{};
        bool pinned_{};
    };
    ea::vector<RowLayout> layouts;
    layouts.reserve(rows_.size());

    float stickyHeight = 0.0f;
    float scrollHeight = 8.0f;
    for (unsigned i = 0; i < rows_.size(); ++i)
    {
        const float height = GetRowHeight(rows_[i].track_);
        if (IsTrackPinned(rows_[i].ref_))
            stickyHeight += height + RowSpacing;
        else
            scrollHeight += height + RowSpacing;
    }

    // Reserve the scrollable extent
    ui::Dummy(ImVec2(0.0f, scrollHeight));
    const float originY = canvasPos.y - ui::GetScrollY();

    float stickyY = visibleRect.Min.y;
    float flowY = originY;
    for (unsigned i = 0; i < rows_.size(); ++i)
    {
        const float height = GetRowHeight(rows_[i].track_);
        const bool pinned = IsTrackPinned(rows_[i].ref_);
        if (pinned)
        {
            layouts.push_back(RowLayout{i, stickyY, stickyY + height, true});
            stickyY += height + RowSpacing;
        }
        else
        {
            layouts.push_back(RowLayout{i, flowY, flowY + height, false});
            flowY += height + RowSpacing;
        }
    }

    const auto rowRectOf = [&](const RowLayout& row)
    {
        return ImRect(ImVec2(canvasPos.x, row.y0_), ImVec2(trackX1, row.y1_));
    };

    RenderBodyGrid(duration, majorStep, minorStep, canvasPos, trackX0, trackX1, visibleRect.Min, visibleRect.Max);

    // Opaque backing of the sticky band. The row tints are semi-transparent, so the rows scrolling
    // past the band would show through a pinned row without it.
    if (stickyHeight > 0.0f)
    {
        const float bandBottom = visibleRect.Min.y + stickyHeight;
        drawList->AddRectFilled(ImVec2(canvasPos.x, visibleRect.Min.y), ImVec2(trackX1, bandBottom), ColorBackground);
        drawList->AddLine(ImVec2(canvasPos.x, bandBottom), ImVec2(trackX1, bandBottom), ColorMrbBorder, 1.0f);
    }

    const bool windowHovered = ui::IsWindowHovered();

    // Track reorder drag: drop the dragged track onto the row under the cursor. UpdateDrag
    // clears the drag on mouse release, so only the in-drag repositioning happens here.
    int highlightRow = -1;
    bool highlightNest = false;
    if (dragKind_ == DragKind::TrackReorder && !layouts.empty())
    {
        TimelineTrack* dragged = TrackAt(timeline_, dragTrack_);
        const float grabY = io.MousePos.y - dragReorderGrab_;
        int anchor = -1;
        const RowLayout* anchorLayout = nullptr;
        for (const RowLayout& row : layouts)
        {
            const float y0 = row.y0_;
            const float y1 = row.y1_;
            if (grabY >= y0 && grabY < y1)
            {
                anchor = static_cast<int>(row.rowIndex_);
                anchorLayout = &row;
                break;
            }
            if (grabY >= y1)
            {
                anchor = static_cast<int>(row.rowIndex_);
                anchorLayout = &row;
            }
        }

        if (dragged)
        {
            TrackRef next;
            TrackRef nestTarget; // group the track is dropped into, expanded when it was collapsed
            bool moved = false;
            const bool belowAll = anchor >= 0 && grabY >= anchorLayout->y1_;
            if (anchor < 0)
            {
                // Above the first row: the track goes to the front of the top level list.
                moved = AddTrackTo(timeline_, dragged, TrackRef{}, 0, next);
            }
            else
            {
                const TrackRow& row = rows_[anchor];
                const ImRect rect = rowRectOf(*anchorLayout);
                const float midY = (rect.Min.y + rect.Max.y) * 0.5f;
                // Dropping onto the upper half of a group row nests the track into that group,
                // the lower half inserts it after the group in the group's own list.
                highlightNest = row.isGroup_ && row.track_ != dragged && grabY < midY
                    && !dragTrack_.IsSelfOrAncestorOf(row.ref_);
                if (row.track_ != dragged)
                {
                    if (highlightNest)
                    {
                        nestTarget = row.ref_;
                        moved = AddTrackTo(timeline_, dragged, row.ref_, M_MAX_UNSIGNED, next);
                    }
                    else
                    {
                        moved = MoveTrackBeside(timeline_, dragged, row.ref_, belowAll || grabY >= midY, next);
                    }
                }
                highlightRow = moved ? anchor : -1;
            }

            if (moved)
            {
                // Reveal the destination of a nested track, otherwise it disappears into a
                // collapsed group without any feedback.
                if (nestTarget.Valid() && IsCollapsed(collapsedTracks_, nestTarget))
                    ToggleCollapsed(collapsedTracks_, nestTarget);
                dragTrack_ = next;
                selectedTrack_ = next;
                RebuildRows();
            }
        }
    }

    // Rows. The sticky band is drawn in the second pass, so a pinned row always ends up on top of
    // the rows that scrolled into its area.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const RowLayout& row : layouts)
        {
            if ((pass == 0) == row.pinned_)
                continue;

            const ImRect rowRect = rowRectOf(row);
            if (rowRect.Max.y < visibleRect.Min.y || rowRect.Min.y > visibleRect.Max.y)
                continue;

            const TrackRow& trackRow = rows_[row.rowIndex_];
            TimelineTrack* track = trackRow.track_;
            if (!track)
                continue;

            const bool rowHovered = windowHovered && rowRect.Contains(io.MousePos);
            const bool rowSelected = trackRow.ref_ == selectedTrack_;

            // Row background
            ImU32 rowColor = (row.rowIndex_ & 1) ? ColorRowOdd : ColorRowEven;
            if (rowSelected)
                rowColor = ColorRowSelected;
            else if (rowHovered)
                rowColor = WithAlpha(rowColor, 255) == rowColor ? (rowColor + ColorRowHover) : rowColor; // additive hover tint
            drawList->AddRectFilled(rowRect.Min, rowRect.Max, rowColor);

            RenderTrackHeader(track, trackRow, rowRect.Min, rowRect.Max, trackX0, rowSelected, windowHovered);
            RenderTrackContent(track, trackRow.ref_, rowRect.Min, rowRect.Max, trackX0, trackX1, rowSelected);
        }
    }

    // Feedback of the reorder drag: the row the dragged track would land on.
    if (dragKind_ == DragKind::TrackReorder && highlightRow >= 0 && highlightRow < static_cast<int>(layouts.size()))
    {
        const ImRect rect = rowRectOf(layouts[static_cast<unsigned>(highlightRow)]);
        const ImU32 tint = highlightNest ? ColorBindingSlotHighlight : ColorRowSelected;
        drawList->AddRectFilled(rect.Min, rect.Max, tint);
        drawList->AddRect(rect.Min, rect.Max, ColorBindingSlotHighlightBorder, 0.0f, 0, 2.0f);
    }

    // Cursor line across the body
    const float cursorX = TimeToX(cursorTime_, trackX0);
    if (cursorX >= trackX0 && cursorX <= trackX1)
        drawList->AddLine(ImVec2(cursorX, visibleRect.Min.y), ImVec2(cursorX, visibleRect.Max.y), ColorCursor, 2.0f);

    // Interactions
    UpdateDrag(trackX0);

    if (windowHovered && dragKind_ == DragKind::None && visibleRect.Contains(io.MousePos))
    {
        // The pinned band overlaps the rows that scrolled up into it, so it owns the cursor first.
        for (int pass = 0; pass < 2; ++pass)
        {
            bool handled = false;
            for (const RowLayout& row : layouts)
            {
                if ((pass == 0) == row.pinned_)
                    continue;

                const ImRect rowRect = rowRectOf(row);
                if (!rowRect.Contains(io.MousePos))
                    continue;

                ProcessRowHitTesting(rows_[row.rowIndex_], rowRect.Min, rowRect.Max, trackX0);
                handled = true;
                break;
            }
            if (handled)
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

void TimelineViewTab::RenderTrackHeader(TimelineTrack* track, const TrackRow& row, const ImVec2& rowMin,
    const ImVec2& rowMax, float trackX0, bool rowSelected, bool windowHovered)
{
    ImGuiIO& io = ui::GetIO();
    ImDrawList* drawList = ui::GetWindowDrawList();
    const ImRect rowRect(rowMin, rowMax);
    const float fontHeight = ui::GetTextLineHeight();
    const float textY = rowMin.y + (rowRect.GetHeight() - fontHeight) * 0.5f;
    const RowMargin margin = ComputeRowMargin(rowMin, rowMax, row);

    // Track type color bar and header text, both offset by the indentation of the nesting level
    drawList->AddRectFilled(ImVec2(margin.colorBarX_, rowMin.y), ImVec2(margin.colorBarX_ + 4.0f, rowMax.y),
        GetTrackColor(track));
    // A pinned row gets a blue top edge across the header, the same accent as its lock button. The
    // boundary of the sticky band is not enough feedback when several rows are pinned.
    if (IsTrackPinned(row.ref_))
        drawList->AddLine(ImVec2(rowMin.x, rowMin.y + 1.0f), ImVec2(trackX0, rowMin.y + 1.0f), ColorMrbLock, 2.0f);
    const ea::string typeText = track->GetTrackTypeName();
    drawList->AddText(ui::GetFont(), ui::GetFontSize(), ImVec2(margin.captionX_, textY), ColorHeaderTypeText, typeText.c_str());
    const ImVec2 typeTextSize = ui::CalcTextSize(typeText.c_str());

    // Expand/collapse arrow of a group row. Only drawn here: ProcessRowHitTesting measures the
    // same rect returned by ComputeRowMargin(), so clicking the arrow cannot start a reorder drag.
    if (row.isGroup_)
    {
        const float halfHeight = (margin.toggleMax_.y - margin.toggleMin_.y) * 0.5f;
        const float centerY = (margin.toggleMin_.y + margin.toggleMax_.y) * 0.5f;
        const float left = margin.toggleMin_.x;
        const ImU32 arrowColor = rowSelected ? ColorHeaderText : WithAlpha(ColorHeaderText, 200);
        if (row.collapsed_)
        {
            drawList->AddTriangleFilled(ImVec2(left, centerY - halfHeight), ImVec2(left, centerY + halfHeight),
                ImVec2(left + halfHeight * 1.4f, centerY), arrowColor);
        }
        else
        {
            const float halfWidth = halfHeight * 0.8f;
            drawList->AddTriangleFilled(ImVec2(left, centerY - halfWidth),
                ImVec2(left + halfWidth * 2.0f, centerY - halfWidth), ImVec2(left + halfWidth, centerY + halfWidth),
                arrowColor);
        }
    }

    // Inline mute / solo / lock controls. Their rects come from the same helper the hit-testing
    // uses, so a click on a letter never reaches the selection or the reorder drag below.
    const bool soloed = IsTrackSoloed(row.ref_);
    const bool locked = IsTrackLocked(row.ref_);
    DrawMrbButton(drawList, margin.muteMin_, margin.muteMax_, "M", track->IsMuted(), ColorMrbMute);
    DrawMrbButton(drawList, margin.soloMin_, margin.soloMax_, "S", soloed, ColorMrbSolo);
    DrawMrbButton(drawList, margin.lockMin_, margin.lockMax_, "L", locked, ColorMrbLock);

    // Binding slot on the right side of the header: drop a node here to bind the track to it
    const ImRect slotRect(ImVec2(trackX0 - BindingSlotWidth - 6.0f, rowMin.y + 2.0f),
        ImVec2(trackX0 - 6.0f, rowMax.y - 2.0f));
    const ea::string& trackBinding = track->GetBindingPath();
    const bool slotOccupied = !trackBinding.empty();
    bool slotHighlighted = false;

    ui::PushID(track);
    if (ui::BeginDragDropTargetCustom(slotRect, ui::GetID("binding_slot")))
    {
        const auto* nodePayload = dynamic_cast<const NodeComponentDragDropPayload*>(DragDropPayload::Get());
        if (nodePayload && !nodePayload->nodes_.empty())
        {
            if (ui::AcceptDragDropPayload(DragDropPayloadType.c_str(),
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                slotHighlighted = true;
                if (ui::GetDragDropPayload()->IsDelivery())
                {
                    Object* dropTarget = static_cast<Object*>(nodePayload->nodes_[0].Get());
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

    // Slot label: the last segment of the binding path, the inherited one for an unbound child of
    // a bound group, or a hint when nothing resolves
    const char* slotLabel = "drop";
    ea::string inheritedLabel;
    if (slotOccupied)
    {
        const auto lastSlash = trackBinding.find_last_of('/');
        slotLabel = lastSlash == ea::string::npos
            ? trackBinding.c_str()
            : trackBinding.c_str() + lastSlash + 1;
    }
    else if (row.ref_.Depth() > 1)
    {
        // The track binds through its enclosing groups: show what it resolves to so the implicit
        // inheritance is not invisible in the header.
        Object* inherited = ResolveInheritedBindingRoot(GetContextPlayer(), timeline_.Get(), row.ref_);
        if (auto* node = dynamic_cast<Node*>(inherited))
            inheritedLabel = node->GetName().empty() ? "inherited" : Format("inherited: {}", node->GetName().c_str());
        else if (inherited)
            inheritedLabel = Format("inherited: {}", inherited->GetTypeName().c_str());
        if (!inheritedLabel.empty())
            slotLabel = inheritedLabel.c_str();
    }
    const ImVec2 slotLabelSize = ui::CalcTextSize(slotLabel);
    if (slotLabelSize.x <= slotRect.GetWidth() - 4.0f)
        drawList->AddText(ImVec2(slotRect.Min.x + (slotRect.GetWidth() - slotLabelSize.x) * 0.5f, textY),
            slotOccupied ? ColorHeaderText : ColorBindingSlotText, slotLabel);

    // Track name, clipped to the space between the inline controls and the binding slot
    const ImVec2 nameTextPos(margin.captionX_ + typeTextSize.x + 8.0f, textY);
    drawList->PushClipRect(ImVec2(margin.controlsEnd_, rowMin.y), ImVec2(slotRect.Min.x - 6.0f, rowMax.y), true);
    drawList->AddText(nameTextPos, track->IsMuted() ? ColorHeaderTypeText : ColorHeaderText, track->GetName().c_str());
    drawList->PopClipRect();

    // Tooltip of the inline controls, measured before the generic header tooltip so the two never
    // compete for the same cursor position.
    const ImRect headerRect(rowMin, ImVec2(trackX0, rowMax.y));
    if (windowHovered && headerRect.Contains(io.MousePos))
    {
        if (ImRect(margin.muteMin_, margin.muteMax_).Contains(io.MousePos))
            ui::SetTooltip("%s the track\nMuted tracks are stored in the timeline resource.",
                track->IsMuted() ? "Unmute" : "Mute");
        else if (ImRect(margin.soloMin_, margin.soloMax_).Contains(io.MousePos))
            ui::SetTooltip("%s solo\nSoloing is a preview only override: while any track is soloed,\n"
                "every other track is silenced. It is never saved.", soloed ? "Drop" : "Add to");
        else if (ImRect(margin.lockMin_, margin.lockMax_).Contains(io.MousePos))
            ui::SetTooltip("%s editing\nA locked track cannot be selected or dragged.\nSession only.",
                locked ? "Unlock" : "Lock");
        else
        {
            ui::SetTooltip("%s track\nName: %s\nAddress: %s\nBinding: %s\nWeight: %.2f%s%s",
                typeText.c_str(), track->GetName().c_str(), row.ref_.ToString().c_str(),
                trackBinding.empty() ? inheritedLabel.empty() ? "(unbound)" : inheritedLabel.c_str() : trackBinding.c_str(),
                track->GetWeight(), track->IsMuted() ? "\n(muted)" : "", locked ? "\n(locked)" : "");
        }
    }
    if (windowHovered && slotRect.Contains(io.MousePos))
    {
        ui::SetTooltip("Binding: %s\nDrag a node from the Scene hierarchy\nand drop it here to bind the track to it.\n"
            "To bind a material, append its attribute and index,\ne.g. \"Model/StaticModel/Material/0\".",
            trackBinding.empty() ? "(unbound)" : trackBinding.c_str());
    }
}

void TimelineViewTab::RenderTrackContent(TimelineTrack* track, const TrackRef& trackRef, const ImVec2& rowMin,
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
        const bool blendsByWeight = TrackBlendsByWeight(track);
        for (unsigned ci = 0; ci < numClips; ++ci)
        {
            const TimelineClip* clip = GetClip(track, ci);
            if (!clip)
                continue;

            const float clipX0 = TimeToX(clip->start_, trackX0);
            const float clipX1 = TimeToX(clip->GetEndTime(), trackX0);
            if (clipX1 < trackX0 || clipX0 > trackX1)
                continue;

            const bool inMulti = multiSelectedClips_.Contains(trackRef, ci);
            const bool clipSelected = (rowSelected && ci == selectedClip_) || inMulti;
            const ImVec2 blockMin(clipX0, rowMin.y + 2.0f);
            const ImVec2 blockMax(clipX1, rowMax.y - 2.0f);

            drawList->AddRectFilled(blockMin, blockMax, WithAlpha(trackColor, 76), ClipRounding);
            drawList->AddRect(blockMin, blockMax, clipSelected ? IM_COL32(255, 255, 255, 255) : WithAlpha(trackColor, 255),
                ClipRounding, 0, clipSelected ? 2.0f : 1.0f);

            const ea::string label = GetClipLabel(track, ci);
            const float blockWidth = blockMax.x - blockMin.x;
            if (!label.empty() && blockWidth > 30.0f)
            {
                // Fit the name inside the clip block (ellipsis when too long) and clip to the block
                // rect, so a very long animation name can no longer overlap the neighbouring clip.
                const ea::string shown = FitText(label.c_str(), blockWidth - 8.0f);
                const ImVec2 textSize = ui::CalcTextSize(shown.c_str());
                const float textX = Clamp((blockMin.x + blockMax.x - textSize.x) * 0.5f,
                    blockMin.x + 4.0f, blockMax.x - 4.0f - textSize.x);
                drawList->PushClipRect(blockMin, blockMax, true);
                drawList->AddText(ImVec2(textX, textY), ColorClipText, shown.c_str());
                drawList->PopClipRect();
            }

            // Trim handles of the selected clip
            if (clipSelected && blockMax.x - blockMin.x > 4.0f * EdgeGrabWidth)
            {
                drawList->AddRectFilled(ImVec2(blockMin.x, blockMin.y + 4.0f), ImVec2(blockMin.x + 3.0f, blockMax.y - 4.0f), IM_COL32(255, 255, 255, 160));
                drawList->AddRectFilled(ImVec2(blockMax.x - 3.0f, blockMin.y + 4.0f), ImVec2(blockMax.x, blockMax.y - 4.0f), IM_COL32(255, 255, 255, 160));
            }

            // Ease ramps of the clip: the top left and bottom right corners are dragged to change
            // easeIn_ / easeOut_, the line inside shows the easing function actually applied.
            if (ClipShowsEase(track, ci) && blockWidth > 24.0f)
                DrawEaseHandle(drawList, ComputeEaseHandles(blockMin, blockMax, *clip), *clip);

            // Speed badge, only when the clip does not play its content at 1:1
            if (Abs(clip->timeScale_ - 1.0f) > 0.001f && blockWidth > 44.0f)
            {
                const ea::string speed = Format("x%.2f", clip->timeScale_);
                const ImVec2 speedSize = ui::CalcTextSize(speed.c_str());
                drawList->AddText(ImVec2(blockMax.x - speedSize.x - 3.0f, blockMin.y + 1.0f), ColorClipText,
                    speed.c_str());
            }

            // Blend: two clips of the same track that overlap are mixed by their weights while both
            // are active. The mixed range is marked so the overlap is not mistaken for a mistake.
            // Tracks that do not blend leave the range unmarked: there the overlap simply means that
            // both clips are active at once.
            for (unsigned other = ci + 1; other < numClips && blendsByWeight; ++other)
            {
                const TimelineClip* next = GetClip(track, other);
                if (!next)
                    continue;
                const float overlap0 = Max(clipX0, TimeToX(next->start_, trackX0));
                const float overlap1 = Min(clipX1, TimeToX(next->GetEndTime(), trackX0));
                if (overlap1 <= overlap0)
                    continue;
                drawList->AddRectFilled(ImVec2(overlap0, blockMin.y), ImVec2(overlap1, blockMax.y), ColorBlendRegion);
                drawList->AddLine(ImVec2(overlap0, blockMax.y), ImVec2(overlap1, blockMin.y), ColorBlendBorder, 1.0f);
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
    RenderTrackDropTarget(track, ImVec2(trackX0, rowMin.y), ImVec2(trackX1, rowMax.y), trackX0);
}

void TimelineViewTab::RenderTrackDropTarget(TimelineTrack* track, const ImVec2& contentMin,
    const ImVec2& contentMax, float trackX0)
{
    auto* animationTrack = dynamic_cast<TimelineAnimationTrack*>(track);
    if (!animationTrack)
        return;

    ImGuiIO& io = ui::GetIO();
    ui::PushID(track);
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

void TimelineViewTab::ProcessRowHitTesting(const TrackRow& row, const ImVec2& rowMin, const ImVec2& rowMax,
    float trackX0)
{
    ImGuiIO& io = ui::GetIO();
    TimelineTrack* track = row.track_;
    const TrackRef& trackRef = row.ref_;
    const bool leftClick = ImGui::IsMouseClicked(0);
    const bool rightClick = ImGui::IsMouseClicked(1);
    const RowMargin margin = ComputeRowMargin(rowMin, rowMax, row);

    // Click on the header selects the track
    if (io.MousePos.x < trackX0)
    {
        // The inline controls swallow the click: neither selection nor the reorder drag may start
        // from them. Their rects are the ones RenderTrackHeader draws.
        if (leftClick)
        {
            if (row.isGroup_ && ImRect(margin.toggleMin_, margin.toggleMax_).Contains(io.MousePos))
            {
                ToggleCollapsed(collapsedTracks_, trackRef);
                RebuildRows();
                return;
            }
            if (ImRect(margin.muteMin_, margin.muteMax_).Contains(io.MousePos))
            {
                BeginModify();
                track->SetMuted(!track->IsMuted());
                return;
            }
            if (ImRect(margin.soloMin_, margin.soloMax_).Contains(io.MousePos))
            {
                ToggleTrackSolo(trackRef);
                return;
            }
            if (ImRect(margin.lockMin_, margin.lockMax_).Contains(io.MousePos))
            {
                ToggleTrackLocked(trackRef);
                return;
            }
        }

        // A locked track takes no selection and starts no drag; the right click still opens its
        // menu, which then only offers to unlock it again.
        if (IsTrackLocked(trackRef))
        {
            if (rightClick)
            {
                popupTrack_ = trackRef;
                ui::OpenPopup("TrackHeaderContextMenu");
            }
            return;
        }

        if (leftClick)
        {
            SelectTrack(trackRef);
            // Drag the header vertically to reorder tracks. Only start when grabbing the
            // name area (left of the binding slot), so the slot keeps its drop behaviour.
            const float slotLeft = trackX0 - BindingSlotWidth - 10.0f;
            if (io.MousePos.x < slotLeft)
            {
                BeginModify();
                dragKind_ = DragKind::TrackReorder;
                dragTrack_ = trackRef;
                dragReorderGrab_ = io.MousePos.y - rowMin.y;
            }
        }
        if (rightClick)
        {
            popupTrack_ = trackRef;
            ui::OpenPopup("TrackHeaderContextMenu");
        }
        return;
    }

    // A locked track takes no clip, key or marker edit either.
    if (IsTrackLocked(trackRef))
        return;

    const float mouseTime = XToTime(io.MousePos.x, trackX0);

    if (auto* markerTrack = AsMarkerTrack(track))
    {
        if (!ProcessMarkerHitTesting(markerTrack, trackRef, rowMin, rowMax, trackX0, leftClick, rightClick))
        {
            if (leftClick)
                SelectTrack(trackRef);
            if (rightClick)
            {
                popupTrack_ = trackRef;
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
        if (ProcessPropertyKeyHitTesting(propertyTrack, trackRef, rowMin, rowMax, trackX0, leftClick))
            return;
    }

    if (ProcessClipHitTesting(track, trackRef, rowMin, rowMax, trackX0, leftClick, rightClick))
        return;

    // Click on the empty track area selects the track
    if (leftClick)
        SelectTrack(trackRef);
    if (rightClick)
    {
        popupTrack_ = trackRef;
        popupClip_ = M_MAX_UNSIGNED;
        popupTime_ = mouseTime;
        ui::OpenPopup("TrackAreaContextMenu");
    }
}

bool TimelineViewTab::ProcessClipHitTesting(TimelineTrack* track, const TrackRef& trackRef, const ImVec2& rowMin,
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
            popupTrack_ = trackRef;
            popupClip_ = ci;
            popupTime_ = XToTime(io.MousePos.x, trackX0);
            ui::OpenPopup("ClipContextMenu");
            return true;
        }

        if (leftClick)
        {
            const ImVec2 blockMin(clipX0, rowMin.y + 2.0f);
            const ImVec2 blockMax(clipX1, rowMax.y - 2.0f);
            const float mouseTime = XToTime(io.MousePos.x, trackX0);

            // Ease ramps are grabbed at the corners of the block. They take priority over the body
            // edits but not over the modifiers that toggle selection or duplicate the clip.
            if (ClipShowsEase(track, ci) && !io.KeyCtrl && !io.KeyAlt && blockMax.x - blockMin.x > 24.0f)
            {
                const EaseHandles handles = ComputeEaseHandles(blockMin, blockMax, *clip);
                if (ImRect(handles.inMin_, handles.inMax_).Contains(io.MousePos))
                {
                    SelectTrack(trackRef);
                    selectedClip_ = ci;
                    BeginModify();
                    dragKind_ = DragKind::EaseIn;
                    dragTrack_ = trackRef;
                    dragClip_ = ci;
                    dragGrabOffset_ = mouseTime - (clip->start_ + clip->easeIn_);
                    return true;
                }
                if (ImRect(handles.outMin_, handles.outMax_).Contains(io.MousePos))
                {
                    SelectTrack(trackRef);
                    selectedClip_ = ci;
                    BeginModify();
                    dragKind_ = DragKind::EaseOut;
                    dragTrack_ = trackRef;
                    dragClip_ = ci;
                    dragGrabOffset_ = mouseTime - (clip->GetEndTime() - clip->easeOut_);
                    return true;
                }
            }

            // Double-clicking a property clip opens the curve editor on it, like the Unity Dope Sheet.
            if (ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                auto* propertyTrack = AsPropertyTrack(track);
                if (propertyTrack && ci < propertyTrack->clips_.size() && !propertyTrack->clips_[ci].curves_.empty())
                {
                    SelectTrack(trackRef);
                    selectedClip_ = ci;
                    selectedKeyTime_ = -1.0f;
                    curveEditor_.Open();
                    return true;
                }
            }

            // Ctrl+click toggles the clip in the multi-selection without starting a drag.
            if (io.KeyCtrl)
            {
                if (!multiSelectedClips_.Erase(trackRef, ci))
                    multiSelectedClips_.Insert(trackRef, ci);
                selectedTrack_ = trackRef;
                selectedClip_ = ci;
                selectedKey_ = M_MAX_UNSIGNED;
                selectedKeyTime_ = -1.0f;
                return true;
            }

            // A clip inside an existing multi-selection keeps the group and drags it as a batch;
            // otherwise the selection collapses to this single clip.
            unsigned dragged = ci;
            if (multiSelectedClips_.Size() > 1 && multiSelectedClips_.Contains(trackRef, ci) && !io.KeyAlt)
            {
                selectedTrack_ = trackRef;
                selectedClip_ = ci;
            }
            else
            {
                SelectTrack(trackRef);
                selectedClip_ = ci;
                multiSelectedClips_.Clear();
                multiSelectedClips_.Insert(trackRef, ci);
            }

            // Alt+drag copies the clip and drags the copy, like Unity. The copy is appended, so the
            // index of the dragged clip changes to the last one of the track.
            if (io.KeyAlt)
            {
                BeginModify();
                DuplicateClipAt(track, ci);
                dragged = GetNumClips(track) - 1;
                clip = GetClip(track, dragged);
                selectedClip_ = dragged;
                multiSelectedClips_.Clear();
                multiSelectedClips_.Insert(trackRef, dragged);
            }

            BeginModify();
            dragTrack_ = trackRef;
            dragClip_ = dragged;
            dragOrigStart_ = clip->start_;
            dragOrigDuration_ = clip->duration_;
            dragOrigClipIn_ = clip->clipIn_;
            dragGrabOffset_ = mouseTime - clip->start_;

            // Snapshot the original start of every selected clip for a batch move.
            dragBatchOrig_.clear();
            for (const ClipKey& key : multiSelectedClips_.Keys())
            {
                if (TimelineClip* selClip = FindSelectedClip(key.track_, key.clip_))
                    dragBatchOrig_.push_back({key, selClip->start_});
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

bool TimelineViewTab::ProcessMarkerHitTesting(TimelineMarkerTrack* markerTrack, const TrackRef& trackRef,
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
                SelectTrack(trackRef);
                selectedKey_ = mi;
                BeginModify();
                dragKind_ = DragKind::Marker;
                dragTrack_ = trackRef;
                dragKey_ = mi;
                dragOrigStart_ = markers[mi].time_;
                dragGrabOffset_ = XToTime(io.MousePos.x, trackX0) - markers[mi].time_;
            }
            return true;
        }
    }
    return false;
}

bool TimelineViewTab::ProcessPropertyKeyHitTesting(TimelinePropertyTrack* propertyTrack, const TrackRef& trackRef,
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
                    SelectTrack(trackRef);
                    selectedClip_ = ci;
                    selectedKeyTime_ = keyTime;
                    BeginModify();
                    dragKind_ = DragKind::Key;
                    dragTrack_ = trackRef;
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

    if (dragKind_ == DragKind::None || !timeline_)
        return;

    // The dragged track is addressed by its path, which the reorder drag keeps up to date while
    // the track moves between lists. A stale reference simply stops the edit.
    TimelineTrack* track = TrackAt(timeline_, dragTrack_);
    if (!track)
        return;
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
            for (auto& [key, origStart] : dragBatchOrig_)
            {
                if (TimelineClip* selClip = FindSelectedClip(key.track_, key.clip_))
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
            // Holding Alt while trimming the left edge keeps the visible content still: instead of
            // sliding the block over the source, the in point of the source moves with the edge.
            if (io.KeyAlt)
                clip->clipIn_ = Max(dragOrigClipIn_ + (newStart - dragOrigStart_), 0.0f);
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
    // Ease ramps are dragged by their tip. The two ramps share the block, so neither may grow past
    // the length the other one already occupies.
    case DragKind::EaseIn:
    {
        if (TimelineClip* clip = GetClip(track, dragClip_))
        {
            const float tip = SnapTime(mouseTime - dragGrabOffset_, snapEnabled);
            clip->easeIn_ = Clamp(tip - clip->start_, 0.0f, clip->duration_ - clip->easeOut_);
        }
        break;
    }
    case DragKind::EaseOut:
    {
        if (TimelineClip* clip = GetClip(track, dragClip_))
        {
            const float tip = SnapTime(mouseTime - dragGrabOffset_, snapEnabled);
            clip->easeOut_ = Clamp(clip->GetEndTime() - tip, 0.0f, clip->duration_ - clip->easeIn_);
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

// Move the play head the way a scrub does: the preview samples the scene from the new time and the
// sequencer scrolls to keep the cursor inside the canvas, so a keyboard step never loses it.
void TimelineViewTab::SetCursorTime(float time)
{
    cursorTime_ = Clamp(time, 0.0f, Max(timeline_->GetDuration(), 0.1f));
    cursorScrubbed_ = true;
    keepCursorVisible_ = true;
}

// Snapping of the sequencer. The member shadows the free SnapTime of the shared header, which is
// how every drag that already calls it picks up the frame grid and the enabled candidate points.
float TimelineViewTab::SnapTime(float time, bool snapEnabled) const
{
    ea::vector<float> candidates;
    if (snapEnabled && (snapToClips_ || snapToKeys_))
    {
        CollectSnapCandidates(candidates, snapToClips_, snapToKeys_);

        // A drag reports its own edges and keys as candidates, and those sit exactly where the item
        // already is. Left in the list they beat every other target on distance and pin the item to
        // the position it started from, so the owned points are dropped first.
        const ea::vector<float> own = DraggedSnapPoints();
        const float dropEps = 0.25f / Max(fps_, 1.0f);
        candidates.erase(ea::remove_if(candidates.begin(), candidates.end(), [&](float candidate)
        {
            for (float point : own)
            {
                if (Abs(candidate - point) <= dropEps)
                    return true;
            }
            return false;
        }), candidates.end());
    }

    // The tolerance is a physical distance, so it is measured in pixels and converted with the
    // current zoom.
    const float tolerance = candidates.empty() ? 0.0f : SnapTolerancePx / Max(pixelsPerSecond_, 0.01f);
    return SnapTimeTo(time, snapEnabled, TimeUnit(), fps_, candidates, tolerance);
}

void TimelineViewTab::CollectSnapCandidates(ea::vector<float>& candidates, bool clipEdges, bool keyPoints) const
{
    candidates.clear();
    if (!timeline_)
        return;

    // Only the visible rows contribute: collapsing a group is how a user gets a dense part of the
    // hierarchy out of the way, and an edit should stop snapping to content that hid itself.
    for (const TrackRow& row : rows_)
    {
        const TimelineTrack* track = row.track_;
        if (!track)
            continue;

        if (clipEdges)
        {
            DispatchClipVector(track, [&](const auto& clips)
            {
                for (const auto& clip : clips)
                {
                    candidates.push_back(clip.start_);
                    candidates.push_back(clip.GetEndTime());
                }
            });
        }

        if (keyPoints)
        {
            // Property keys are stored relative to their clip, markers are absolute. Both are
            // features a user aims at, so they end up in the same list.
            if (const auto* propertyTrack = dynamic_cast<const TimelinePropertyTrack*>(track))
            {
                for (const TimelinePropertyClip& clip : propertyTrack->clips_)
                {
                    for (float localTime : GetPropertyKeyTimes(clip))
                        candidates.push_back(clip.start_ + localTime);
                }
            }
            else if (const auto* markerTrack = dynamic_cast<const TimelineMarkerTrack*>(track))
            {
                for (const TimelineMarker& marker : markerTrack->GetMarkers())
                    candidates.push_back(marker.time_);
            }
        }
    }

    ea::sort(candidates.begin(), candidates.end());
    candidates.erase(ea::unique(candidates.begin(), candidates.end()), candidates.end());
}

// The time points the active drag owns. SnapTime takes them out of the candidate list, because an
// item that can snap to itself can never reach anything else.
ea::vector<float> TimelineViewTab::DraggedSnapPoints() const
{
    ea::vector<float> points;
    if (!timeline_ || dragKind_ == DragKind::None)
        return points;

    TimelineTrack* track = TrackAt(timeline_, dragTrack_);
    if (!track)
        return points;

    // Both edges of a whole block: the end travels with the start, so neither is a target.
    const auto pushClipEdges = [&](const TimelineClip* clip)
    {
        if (clip)
        {
            points.push_back(clip->start_);
            points.push_back(clip->GetEndTime());
        }
    };

    switch (dragKind_)
    {
    case DragKind::ClipMove:
        pushClipEdges(GetClip(track, dragClip_));
        // A batch move shifts every selected clip by the same delta, so none of them may hold
        // itself back either.
        for (const auto& [key, origStart] : dragBatchOrig_)
            pushClipEdges(GetClip(TrackAt(timeline_, key.track_), key.clip_));
        break;

    // A trim keeps one edge in place. The fixed edge is a legitimate target and only the moving one
    // is dropped.
    case DragKind::ClipTrimStart:
        if (const TimelineClip* clip = GetClip(track, dragClip_))
            points.push_back(clip->start_);
        break;

    case DragKind::ClipTrimEnd:
        if (const TimelineClip* clip = GetClip(track, dragClip_))
            points.push_back(clip->GetEndTime());
        break;

    // An ease ramp is dragged by its tip, which is a point of its own inside the block.
    case DragKind::EaseIn:
        if (const TimelineClip* clip = GetClip(track, dragClip_))
            points.push_back(clip->start_ + clip->easeIn_);
        break;

    case DragKind::EaseOut:
        if (const TimelineClip* clip = GetClip(track, dragClip_))
            points.push_back(clip->GetEndTime() - clip->easeOut_);
        break;

    case DragKind::Key:
    {
        TimelinePropertyTrack* propertyTrack = AsPropertyTrack(track);
        if (propertyTrack && dragClip_ < propertyTrack->clips_.size() && selectedKeyTime_ >= 0.0f)
            points.push_back(propertyTrack->clips_[dragClip_].start_ + selectedKeyTime_);
        break;
    }

    case DragKind::Marker:
    {
        TimelineMarkerTrack* markerTrack = AsMarkerTrack(track);
        if (markerTrack && dragKey_ < markerTrack->GetNumMarkers())
            points.push_back(markerTrack->GetMarkers()[dragKey_].time_);
        break;
    }

    default:
        break;
    }
    return points;
}

// Next or previous keyframe of the timeline. The bracket keys step over the authored features
// regardless of what the snapping is set to, and clip edges are deliberately left out: they would
// turn a single step into a jump across the whole sequence.
float TimelineViewTab::StepToNextKey(float time, bool forward) const
{
    ea::vector<float> times;
    CollectSnapCandidates(times, false, true);
    if (times.empty())
        return time;

    // Half a frame of slack, so standing exactly on a key steps to the next one instead of
    // reporting the current position as the result.
    const float eps = 0.5f / Max(fps_, 1.0f);
    if (forward)
    {
        for (float candidate : times)
        {
            if (candidate > time + eps)
                return candidate;
        }
    }
    else
    {
        for (auto it = times.rbegin(); it != times.rend(); ++it)
        {
            if (*it < time - eps)
                return *it;
        }
    }
    return time;
}

// Frame the interesting part of the timeline: the selected clips when there are any, the loop
// region otherwise, the whole resource when neither is set.
void TimelineViewTab::FitViewToSelection()
{
    float from = FLT_MAX;
    float to = -FLT_MAX;

    const ea::vector<ClipKey> keys = SelectedClipKeys();
    for (const ClipKey& key : keys)
    {
        const TimelineClip* clip = FindSelectedClip(key.track_, key.clip_);
        if (!clip)
            continue;
        from = Min(from, clip->start_);
        to = Max(to, clip->GetEndTime());
    }

    if (to <= from)
    {
        if (loopEnd_ > loopStart_)
        {
            from = loopStart_;
            to = loopEnd_;
        }
        else
        {
            from = 0.0f;
            to = timeline_->GetDuration();
        }
    }

    to = Max(to, from + MinClipDuration);
    const float margin = (to - from) * 0.05f;
    from = Max(from - margin, 0.0f);
    to += margin;

    const float trackAreaWidth = Max(ui::GetContentRegionAvail().x - HeaderWidth - 20.0f, 100.0f);
    pixelsPerSecond_ = Clamp(trackAreaWidth / Max(to - from, 0.1f), MinPixelsPerSecond, MaxPixelsPerSecond);
    viewStartTime_ = from;
}

void TimelineViewTab::ProcessDeleteKey()
{
    if (!ui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) || !ui::IsKeyPressed(ImGuiKey_Delete) || !timeline_)
        return;

    TimelineTrack* track = TrackAt(timeline_, selectedTrack_);
    if (!track)
        return;

    // Batch delete of the multi-selected clips (takes precedence over single selection).
    if (multiSelectedClips_.Size() > 1)
    {
        BeginModify();
        // Group the clips per track and erase each track's clips from the highest index down, so
        // an erasure never invalidates the indices still pending. Tracks are collected in the
        // selection order and the selection itself keeps the order the clips were picked in.
        ea::vector<TrackRef> tracks;
        for (const ClipKey& key : multiSelectedClips_.Keys())
        {
            bool seen = false;
            for (const TrackRef& existing : tracks)
                seen = seen || existing == key.track_;
            if (!seen)
                tracks.push_back(key.track_);
        }

        for (const TrackRef& ref : tracks)
        {
            TimelineTrack* clipTrack = TrackAt(timeline_, ref);
            if (!clipTrack)
                continue;
            ea::vector<unsigned> clips;
            for (const ClipKey& key : multiSelectedClips_.Keys())
            {
                if (key.track_ == ref)
                    clips.push_back(key.clip_);
            }
            ea::sort(clips.begin(), clips.end());
            for (auto it = clips.rbegin(); it != clips.rend(); ++it)
                RemoveClipAt(clipTrack, *it);
        }
        multiSelectedClips_.Clear();
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

    ImGuiIO& io = ui::GetIO();
    // A text field owns the keyboard while it is being edited: the navigation keys below are plain
    // letters and punctuation, so they must not leak into it.
    if (io.WantTextInput)
        return;

    if (io.KeyCtrl)
    {
        if (ui::IsKeyPressed(ImGuiKey_C))
            CopySelectedClips();
        else if (ui::IsKeyPressed(ImGuiKey_V))
            PasteClips(cursorTime_);
        else if (ui::IsKeyPressed(ImGuiKey_D))
            DuplicateSelectedClip();
        return;
    }

    // Cursor navigation. None of it touches the resource, so no undo action is started.
    const float secPerFrame = 1.0f / Max(fps_, 1.0f);
    if (ui::IsKeyPressed(ImGuiKey_Home))
        SetCursorTime(0.0f);
    else if (ui::IsKeyPressed(ImGuiKey_End))
        SetCursorTime(timeline_->GetDuration());
    else if (ui::IsKeyPressed(ImGuiKey_Comma))
        SetCursorTime(Max(cursorTime_ - secPerFrame, 0.0f));
    else if (ui::IsKeyPressed(ImGuiKey_Period))
        SetCursorTime(cursorTime_ + secPerFrame);
    else if (ui::IsKeyPressed(ImGuiKey_LeftBracket))
        SetCursorTime(StepToNextKey(cursorTime_ - secPerFrame, false));
    else if (ui::IsKeyPressed(ImGuiKey_RightBracket))
        SetCursorTime(StepToNextKey(cursorTime_ + secPerFrame, true));
    else if (ui::IsKeyPressed(ImGuiKey_F) && io.KeyShift)
        FitViewToSelection();
    else if (ui::IsKeyPressed(ImGuiKey_S))
        AddKeyRowAtCursor();
}

// The keyboard counterpart of the two "Add Key" context menu entries. The play head is already
// where the key belongs - it is moved with the navigation keys and stepped onto features with the
// brackets -, so no snapping is applied here.
void TimelineViewTab::AddKeyRowAtCursor()
{
    auto* propertyTrack = AsPropertyTrack(TrackAt(timeline_, selectedTrack_));
    if (!propertyTrack || selectedClip_ >= propertyTrack->clips_.size())
        return;

    auto& clip = propertyTrack->clips_[selectedClip_];
    if (clip.curves_.empty())
        return;

    const float localTime = Clamp(cursorTime_ - clip.start_, 0.0f, clip.duration_);
    if (HasPropertyKeyRowAt(clip, localTime))
        return;

    BeginModify();
    AddPropertyKeyAt(clip, localTime);
    selectedKeyTime_ = localTime;
}

// The clips a batch edit applies to: the multi-selection when there is one, otherwise the single
// clip selected in the sequencer. Keeps every batch command working on the plain selection too.
ea::vector<ClipKey> TimelineViewTab::SelectedClipKeys() const
{
    if (multiSelectedClips_.Size() > 1)
        return multiSelectedClips_.Keys();
    ea::vector<ClipKey> keys;
    if (selectedClip_ != M_MAX_UNSIGNED && selectedTrack_.Valid())
        keys.push_back(ClipKey{selectedTrack_, selectedClip_});
    return keys;
}

// Cut every selected clip at the given timeline time. Clips that do not span the cut are left out,
// so running the command on a whole multi-selection only splits the ones under the cursor.
void TimelineViewTab::SplitSelectedClips(float splitTime)
{
    if (!timeline_)
        return;
    const ea::vector<ClipKey> keys = SelectedClipKeys();
    if (keys.empty())
        return;

    // Group the clips per track and split each track from the highest index down: the second half
    // is inserted right after the clip it was cut from, which only shifts clips already processed.
    BeginModify();
    ea::vector<TrackRef> tracks;
    for (const ClipKey& key : keys)
    {
        if (!ContainsRef(tracks, key.track_))
            tracks.push_back(key.track_);
    }
    for (const TrackRef& ref : tracks)
    {
        TimelineTrack* track = TrackAt(timeline_.Get(), ref);
        if (!track)
            continue;
        ea::vector<unsigned> indices;
        for (const ClipKey& key : keys)
        {
            if (key.track_ == ref)
                indices.push_back(key.clip_);
        }
        ea::sort(indices.begin(), indices.end());
        for (auto it = indices.rbegin(); it != indices.rend(); ++it)
            SplitClipAt(track, *it, splitTime);
    }
    // The first half keeps the index of the clip it was cut from, so the selection still points at
    // content that exists; the freshly created halves stay unselected to keep the choice deliberate.
    multiSelectedClips_.Clear();
}

// Copy every selected clip to the block that immediately follows it, the way Unity duplicates.
void TimelineViewTab::DuplicateSelectedClip()
{
    if (!timeline_)
        return;
    const ea::vector<ClipKey> keys = SelectedClipKeys();
    if (keys.empty())
        return;

    BeginModify();
    // Copies are appended to the track, so duplicating a batch never invalidates an index the loop
    // is still going to read.
    multiSelectedClips_.Clear();
    for (const ClipKey& key : keys)
    {
        TimelineTrack* track = TrackAt(timeline_.Get(), key.track_);
        if (!track || key.clip_ >= GetNumClips(track))
            continue;
        DuplicateClipAt(track, key.clip_);
        const unsigned copy = GetNumClips(track) - 1;
        multiSelectedClips_.Insert(key.track_, copy);
        selectedTrack_ = key.track_;
        selectedClip_ = copy;
    }
}

// Small floating dialog that enters the playback speed of one clip. It is a window rather than a
// popup because it is opened from a menu, whose id scope a popup would be registered under.
void TimelineViewTab::RenderClipSpeedDialog()
{
    if (!speedEditOpen_)
        return;

    TimelineTrack* track = TrackAt(timeline_.Get(), speedEditTrack_);
    TimelineClip* clip = track ? GetClip(track, speedEditClip_) : nullptr;
    if (!clip)
    {
        speedEditOpen_ = false;
        return;
    }

    ui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (speedEditFocusPending_)
    {
        ui::SetNextWindowFocus();
        speedEditFocusPending_ = false;
    }

    bool open = true;
    if (!ui::Begin("Clip Speed", &open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ui::End();
        speedEditOpen_ = open;
        return;
    }

    ui::TextDisabled("%s  (%.2f s, in %.2f s)", GetClipLabel(track, speedEditClip_).c_str(), clip->duration_,
        clip->clipIn_);
    const float sourceSpan = clip->duration_ * clip->timeScale_;
    ui::TextDisabled("source span: %.2f s", sourceSpan);
    ui::DragFloat("Speed", &speedEditValue_, 0.01f, 0.05f, 20.0f, "%.2f x");

    // The two modes differ in what the block keeps: its length in the timeline, or the amount of
    // source content it consumes.
    int mode = speedEditKeepDuration_ ? 0 : 1;
    ui::RadioButton("Keep clip length (play faster)", &mode, 0);
    ui::RadioButton("Keep source span (block shrinks)", &mode, 1);
    speedEditKeepDuration_ = mode == 0;

    if (ui::Button("Apply"))
    {
        BeginModify();
        SetClipSpeed(track, speedEditClip_, speedEditValue_, speedEditKeepDuration_);
    }
    ui::SameLine();
    if (ui::Button("OK") || (ui::IsWindowFocused() && ui::IsKeyPressed(ImGuiKey_Enter)))
    {
        BeginModify();
        SetClipSpeed(track, speedEditClip_, speedEditValue_, speedEditKeepDuration_);
        open = false;
    }
    ui::SameLine();
    if (ui::Button("Cancel"))
        open = false;

    ui::End();
    speedEditOpen_ = open;
}

void TimelineViewTab::RenderTrackAreaContextMenu()
{
    if (!ui::BeginPopup("TrackAreaContextMenu"))
        return;

    const bool snapEnabled = !ui::GetIO().KeyCtrl;
    TimelineTrack* track = TrackAt(timeline_.Get(), popupTrack_);

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
            auto* propertyTrack = AsPropertyTrack(track);
            const bool canAddKey = targetClip != M_MAX_UNSIGNED && propertyTrack
                && targetClip < propertyTrack->clips_.size()
                && !propertyTrack->clips_[targetClip].curves_.empty();
            // The snapped time is what the label offers and what the insert writes, and a row that
            // already sits there would make the entry a silent no-op, so it is greyed out instead.
            TimelinePropertyClip* propertyClip = canAddKey ? &propertyTrack->clips_[targetClip] : nullptr;
            const float keyTime = propertyClip ? SnapTime(popupTime_, snapEnabled) : 0.0f;
            const float localKeyTime = propertyClip
                ? Clamp(keyTime - propertyClip->start_, 0.0f, propertyClip->duration_) : 0.0f;
            const bool rowExists = propertyClip && HasPropertyKeyRowAt(*propertyClip, localKeyTime);
            // One name for one edit: the clip menu offers the same entry as "Add Key", so the two
            // never read like two different operations.
            const ea::string label = Format("Add Key at {}", FormatTime(keyTime).c_str());
            ui::BeginDisabled(!canAddKey || rowExists);
            if (ui::Selectable(label.c_str()) && canAddKey && !rowExists)
            {
                BeginModify();
                AddPropertyKeyAt(*propertyClip, localKeyTime);
                SelectTrack(popupTrack_);
                selectedClip_ = targetClip;
                selectedKeyTime_ = localKeyTime;
            }
            // Disabled entries still have to explain themselves, otherwise the greyed out line reads
            // like a bug rather than as a rule of the data model.
            if (rowExists && ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ui::SetTooltip("A key row already exists at this time.");
            else if (!canAddKey && ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ui::SetTooltip(targetClip == M_MAX_UNSIGNED
                    ? "The time does not fall inside a clip, and every key belongs to one."
                    : "The clip at this time has no curves yet.");
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
            RemoveTrackAt(popupTrack_);
    }
    ui::EndPopup();
}

void TimelineViewTab::RenderClipContextMenu()
{
    if (!ui::BeginPopup("ClipContextMenu"))
        return;

    TimelineTrack* track = TrackAt(timeline_.Get(), popupTrack_);
    if (track && popupClip_ < GetNumClips(track))
    {
        TimelineClip* clip = GetClip(track, popupClip_);

        // Property clips with curves offer the curve editor before the ordinary clip actions.
        auto* propertyTrack = AsPropertyTrack(track);
        const bool hasCurves = propertyTrack && popupClip_ < propertyTrack->clips_.size()
            && !propertyTrack->clips_[popupClip_].curves_.empty();
        if (hasCurves && ui::Selectable("Open Curve Editor"))
        {
            SelectTrack(popupTrack_);
            selectedClip_ = popupClip_;
            selectedKeyTime_ = -1.0f;
            curveEditor_.Open();
        }
        // Authoring a key is the highest frequency edit there is on a property track, and a long
        // block covers the empty row area which alone offered the insert entry. The block menu
        // therefore repeats it for the time of the click.
        if (hasCurves)
        {
            auto& propertyClip = propertyTrack->clips_[popupClip_];
            const float keyTime = SnapTime(popupTime_, !ui::GetIO().KeyCtrl);
            const float localTime = Clamp(keyTime - propertyClip.start_, 0.0f, propertyClip.duration_);
            const bool rowExists = HasPropertyKeyRowAt(propertyClip, localTime);
            ui::BeginDisabled(rowExists);
            const ea::string keyLabel = Format("Add Key at {}", FormatTime(keyTime).c_str());
            if (ui::Selectable(keyLabel.c_str()) && !rowExists)
            {
                BeginModify();
                SelectTrack(popupTrack_);
                selectedClip_ = popupClip_;
                AddPropertyKeyAt(propertyClip, localTime);
                selectedKeyTime_ = localTime;
            }
            if (ui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (rowExists)
                    ui::SetTooltip("A key row already exists at this time.\nDrag it in the curve editor or the Dope Sheet to move it.");
                else
                    ui::SetTooltip("Insert a key row into every curve of the clip at the clicked time.\n"
                        "The value is copied from the nearest key, so it does not move the object.\n"
                        "Hold Ctrl to place it without snapping.");
            }
            ui::EndDisabled();
        }
        // The sheet is not bound to a selection, it lists the whole hierarchy, so opening it from a
        // clip only brings the window up.
        if (ui::Selectable("Open Dope Sheet"))
            dopeSheet_.Open();
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

        ui::Separator();
        // A cut only makes sense strictly inside the block, otherwise it would shave off a clip of
        // zero length. The entry stays visible with its position in the label so that the command
        // can be found, and is merely disabled when the cursor is elsewhere.
        if (clip)
        {
            const float cut = cursorTime_ - clip->start_;
            const bool canSplit = cut > MinClipDuration && clip->duration_ - cut > MinClipDuration;
            const ea::string label = Format("Split at Cursor (%s)", FormatTime(cursorTime_).c_str());
            ui::BeginDisabled(!canSplit);
            if (ui::Selectable(label.c_str()) && canSplit)
            {
                BeginModify();
                SplitClipAt(track, popupClip_, cursorTime_);
            }
            ui::EndDisabled();
            if (ui::Selectable("Ripple Delete"))
            {
                BeginModify();
                RippleDeleteClipAt(track, popupClip_);
                selectedClip_ = M_MAX_UNSIGNED;
                selectedKeyTime_ = -1.0f;
                multiSelectedClips_.Clear();
            }
            if (ui::Selectable("Eliminate Gaps"))
            {
                BeginModify();
                EliminateGaps(track);
            }

            ui::Separator();
            // Easing: the shape of the two ramps is picked here, their lengths stay editable as
            // sliders and as the corners of the block. Tracks which do not blend by weight have no
            // use for the ramps at all, and a property clip only eases the cross-fade with an
            // overlapping neighbour. The weight is different: zero also stops the clip from
            // contributing anything, so it stays on every blending track.
            const bool blendsByWeight = TrackBlendsByWeight(track);
            const bool showsEase = ClipShowsEase(track, popupClip_);
            if (showsEase && ui::BeginMenu("Easing"))
            {
                for (unsigned i = 0; TIMELINE_EASE_NAMES[i]; ++i)
                {
                    ea::string item = TIMELINE_EASE_NAMES[i];
                    if (static_cast<unsigned>(clip->ease_) == i)
                        item += "  *";
                    if (ui::Selectable(item.c_str()))
                    {
                        BeginModify();
                        clip->ease_ = static_cast<TimelineEase>(i);
                    }
                }
                ui::Separator();
                if (ui::Selectable("Clear Both Ramps"))
                {
                    BeginModify();
                    clip->easeIn_ = 0.0f;
                    clip->easeOut_ = 0.0f;
                }
                ui::EndMenu();
            }
            if (showsEase)
            {
                // Each ramp may take the length the other one has not used yet.
                ui::SetNextItemWidth(170.0f);
                float easeIn = clip->easeIn_;
                if (ui::SliderFloat("Ease In", &easeIn, 0.0f, Max(clip->duration_ - clip->easeOut_, 0.0f),
                        "%.2f s")
                    && easeIn != clip->easeIn_)
                {
                    BeginModify();
                    clip->easeIn_ = easeIn;
                }
                ui::SetNextItemWidth(170.0f);
                float easeOut = clip->easeOut_;
                if (ui::SliderFloat("Ease Out", &easeOut, 0.0f, Max(clip->duration_ - clip->easeIn_, 0.0f),
                        "%.2f s")
                    && easeOut != clip->easeOut_)
                {
                    BeginModify();
                    clip->easeOut_ = easeOut;
                }
            }
            if (blendsByWeight)
            {
                ui::SetNextItemWidth(170.0f);
                float weight = clip->weight_;
                if (ui::SliderFloat("Weight", &weight, 0.0f, 1.0f, "%.2f") && weight != clip->weight_)
                {
                    BeginModify();
                    clip->weight_ = Clamp(weight, 0.0f, 1.0f);
                }
            }

            ui::Separator();
            // The two speed entries only differ in what the dialog preserves; they open it with the
            // mode and the current value preset.
            if (ui::Selectable("Speed (Keep Clip Length)..."))
            {
                speedEditOpen_ = true;
                speedEditFocusPending_ = true;
                speedEditTrack_ = popupTrack_;
                speedEditClip_ = popupClip_;
                speedEditKeepDuration_ = true;
                speedEditValue_ = clip->timeScale_;
            }
            if (ui::Selectable("Speed (Keep Source Span)..."))
            {
                speedEditOpen_ = true;
                speedEditFocusPending_ = true;
                speedEditTrack_ = popupTrack_;
                speedEditClip_ = popupClip_;
                speedEditKeepDuration_ = false;
                speedEditValue_ = clip->timeScale_;
            }
        }
    }
    ui::EndPopup();
}

void TimelineViewTab::RenderTrackHeaderContextMenu()
{
    if (!ui::BeginPopup("TrackHeaderContextMenu"))
        return;

    TimelineTrack* track = TrackAt(timeline_.Get(), popupTrack_);
    if (track)
    {
        // A locked track only offers to be unlocked again: nothing editable may be reached from
        // the header menu of a protected row.
        if (IsTrackLocked(popupTrack_))
        {
            if (ui::Selectable("Unlock Track"))
                ToggleTrackLocked(popupTrack_);
            ui::EndPopup();
            return;
        }

        if (ui::Selectable(track->IsMuted() ? "Unmute Track" : "Mute Track"))
        {
            BeginModify();
            track->SetMuted(!track->IsMuted());
        }
        if (ui::Selectable("Delete Track"))
            RemoveTrackAt(popupTrack_);
        // Pinning is view state, so it is offered next to the other view entries rather than with
        // the edits that go through BeginModify.
        if (ui::Selectable(IsTrackPinned(popupTrack_) ? "Unpin Track Header" : "Pin Track Header"))
            ToggleTrackPinned(popupTrack_);
        ui::Separator();
        if (ui::Selectable("Open Dope Sheet"))
            dopeSheet_.Open();
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

// Marker list of the whole hierarchy, between the sequencer and the properties panel. Editing a
// marker here changes exactly the same resource the lane in the sequencer draws, only it stays
// reachable when the timeline has more markers than the narrow marker row can show at once.
void TimelineViewTab::RenderMarkersPanel()
{
    if (!timeline_)
        return;

    // Snapshot the tracks first: adding a marker track rebuilds the row list the panel walks over.
    ea::vector<TimelineMarkerTrack*> markerTracks;
    unsigned markerCount = 0;
    for (const TrackRow& row : rows_)
    {
        TimelineMarkerTrack* markerTrack = AsMarkerTrack(row.track_);
        if (!markerTrack)
            continue;
        markerTracks.push_back(markerTrack);
        markerCount += markerTrack->GetNumMarkers();
    }

    const float panelWidth = ui::GetContentRegionAvail().x;
    ui::Separator();

    if (ui::ArrowButton("markers_panel_toggle", markersPanelOpen_ ? ImGuiDir_Down : ImGuiDir_Right))
        markersPanelOpen_ = !markersPanelOpen_;
    if (ui::IsItemHovered())
        ui::SetTooltip("Show or hide the marker list.");
    ui::SameLine();
    ui::Text("Markers (%u)", markerCount);
    ui::SameLine();
    if (ui::SmallButton("Add at cursor"))
    {
        TimelineMarkerTrack* target = markerTracks.empty() ? nullptr : markerTracks.front();
        if (!target)
        {
            // A timeline without a marker track gets one on the first use, so the button never has
            // to explain what a marker track is.
            AddTrackOfType("Marker", TrackRef{});
            for (const TrackRow& row : rows_)
            {
                TimelineMarkerTrack* created = AsMarkerTrack(row.track_);
                if (created)
                {
                    target = created;
                    break;
                }
            }
        }
        if (target)
        {
            BeginModify();
            target->AddMarker(TimelineMarker{MakeUniqueMarkerName(*target), cursorTime_});
        }
    }
    if (ui::IsItemHovered())
        ui::SetTooltip("Insert a marker at the play head, in the first marker track.\n"
            "Creates a marker track when the timeline has none.");
    ui::SameLine();
    if (ui::SmallButton("Snap to Cursor"))
    {
        // Move the marker nearest to the play head onto it, which is how a beat found while
        // scrubbing gets labelled without leaving the keyboard.
        TimelineMarker* best = nullptr;
        float bestDelta = FLT_MAX;
        for (TimelineMarkerTrack* markerTrack : markerTracks)
        {
            for (TimelineMarker& marker : markerTrack->GetMarkers())
            {
                const float delta = Abs(marker.time_ - cursorTime_);
                if (delta < bestDelta)
                {
                    bestDelta = delta;
                    best = &marker;
                }
            }
        }
        if (best)
        {
            BeginModify();
            best->time_ = cursorTime_;
        }
    }
    if (ui::IsItemHovered())
        ui::SetTooltip("Move the marker closest to the play head onto it.");

    if (!markersPanelOpen_)
        return;

    ui::BeginChild("timeline_markers_panel", ImVec2(panelWidth, MarkersPanelHeight), ImGuiChildFlags_None, 0);
    if (markerTracks.empty())
    {
        ui::TextDisabled("No marker track yet.");
        ui::EndChild();
        return;
    }

    // The time column follows the ruler unit, so the list and the sequencer never disagree about
    // where a marker is. The conversion happens on the way in and out: the resource keeps seconds.
    const bool inFrames = TimeUnit() == TimelineTimeUnit::Frames;

    if (ui::BeginTable("markers_table", 4,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit))
    {
        ui::TableSetupColumn("Track");
        ui::TableSetupColumn("Marker", ImGuiTableColumnFlags_WidthStretch);
        ui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed);
        ui::TableSetupColumn("At", ImGuiTableColumnFlags_WidthFixed);
        for (TimelineMarkerTrack* markerTrack : markerTracks)
        {
            const ea::string trackName = markerTrack->GetName().empty()
                ? markerTrack->GetTrackTypeName()
                : markerTrack->GetName();
            auto& markers = markerTrack->GetMarkers();
            ui::PushID(markerTrack);
            for (unsigned mi = 0; mi < markers.size(); ++mi)
            {
                TimelineMarker& marker = markers[mi];
                ui::PushID(static_cast<int>(mi));
                ui::TableNextRow();

                ui::TableNextColumn();
                ui::TextUnformatted(trackName.c_str());

                ui::TableNextColumn();
                {
                    ea::string name = marker.name_;
                    if (EditString("##markerPanelName", name))
                        marker.name_ = name;
                    // Double clicking the name field jumps the play head, the way clicking a
                    // marker in the lane does.
                    if (ui::IsItemHovered() && ui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        SetCursorTime(marker.time_);
                }

                ui::TableNextColumn();
                {
                    float shown = inFrames ? marker.time_ * fps_ : marker.time_;
                    if (ui::DragFloat("##markerPanelTime", &shown, inFrames ? 1.0f : 0.01f, 0.0f, FLT_MAX,
                            inFrames ? "%.0f f" : "%.2f s"))
                    {
                        BeginModify();
                        marker.time_ = Max(inFrames ? shown / Max(fps_, 1.0f) : shown, 0.0f);
                    }
                }

                ui::TableNextColumn();
                if (ui::SmallButton("Go"))
                    SetCursorTime(marker.time_);
                if (ui::IsItemHovered())
                    ui::SetTooltip("Move the play head to this marker.");
                ui::SameLine();
                bool removed = false;
                if (ui::SmallButton("X"))
                {
                    BeginModify();
                    markerTrack->RemoveMarker(mi);
                    removed = true;
                }
                if (ui::IsItemHovered())
                    ui::SetTooltip("Delete this marker.");
                ui::PopID();
                // Erasing shifts the rest of the list up, so the same index is visited again: the
                // marker that moved into this row must be drawn here, not skipped.
                if (removed)
                    --mi;
            }
            ui::PopID();
        }
        ui::EndTable();
    }
    ui::EndChild();
}

void TimelineViewTab::RenderSelectionPanel()
{
    ui::Separator();
    ui::BeginChild("timeline_selection", ImVec2(ui::GetContentRegionAvail().x, 0.0f), ImGuiChildFlags_None, 0);

    if (!timeline_ || !TrackAt(timeline_.Get(), selectedTrack_))
    {
        ui::TextDisabled("Select a track to edit its properties");
        ui::EndChild();
        return;
    }

    TimelineTrack* track = TrackAt(timeline_.Get(), selectedTrack_);

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

    // Unity-aligned extrapolation selector: what plays when the block reaches past the end of
    // the clip content (for animation clips: past the end of the .ani asset).
    {
        int extra = static_cast<int>(clip->extrapolation_);
        ui::SameLine();
        ui::SetNextItemWidth(150.0f);
        if (ui::Combo("##clipExtrapolation", &extra, TIMELINE_EXTRAPOLATION_NAMES, 5))
        {
            BeginModify();
            clip->extrapolation_ = static_cast<TimelineExtrapolation>(extra);
        }
    }

    // Second row: how the block consumes its source and, where the track mixes its clips by
    // weight, how strongly it blends with the neighbours. clipIn_ is what an Alt trimmed left edge
    // moves, timeScale_ is what the Speed dialog writes, weight_ is the blend factor of an overlap.
    {
        ui::SetNextItemWidth(110.0f);
        float clipIn = clip->clipIn_;
        if (ui::DragFloat("In", &clipIn, 0.01f, 0.0f, FLT_MAX, "%.2f s"))
        {
            BeginModify();
            clip->clipIn_ = Max(clipIn, 0.0f);
        }
        ui::SameLine();
        ui::SetNextItemWidth(90.0f);
        float speed = clip->timeScale_;
        if (ui::DragFloat("Speed", &speed, 0.01f, 0.05f, 20.0f, "%.2f x"))
        {
            BeginModify();
            clip->timeScale_ = Clamp(speed, 0.05f, 20.0f);
        }
        if (TrackBlendsByWeight(track))
        {
            ui::SameLine();
            ui::SetNextItemWidth(120.0f);
            float weight = clip->weight_;
            if (ui::SliderFloat("Weight", &weight, 0.0f, 1.0f, "%.2f"))
            {
                BeginModify();
                clip->weight_ = Clamp(weight, 0.0f, 1.0f);
            }
        }
    }
    // Third row: the two ramps of the block. Both are measured in seconds from their own end, so
    // each of them may only take the length the other one has left over.
    if (ClipShowsEase(track, selectedClip_))
    {
        ui::SetNextItemWidth(110.0f);
        float easeIn = clip->easeIn_;
        if (ui::DragFloat("Ease In", &easeIn, 0.01f, 0.0f, clip->duration_, "%.2f s"))
        {
            BeginModify();
            clip->easeIn_ = Clamp(easeIn, 0.0f, Max(clip->duration_ - clip->easeOut_, 0.0f));
        }
        ui::SameLine();
        ui::SetNextItemWidth(110.0f);
        float easeOut = clip->easeOut_;
        if (ui::DragFloat("Ease Out", &easeOut, 0.01f, 0.0f, clip->duration_, "%.2f s"))
        {
            BeginModify();
            clip->easeOut_ = Clamp(easeOut, 0.0f, Max(clip->duration_ - clip->easeIn_, 0.0f));
        }
        ui::SameLine();
        int ease = static_cast<int>(clip->ease_);
        ui::SetNextItemWidth(150.0f);
        if (ui::Combo("##clipEase", &ease, TIMELINE_EASE_NAMES, static_cast<int>(NameCount(TIMELINE_EASE_NAMES))))
        {
            BeginModify();
            clip->ease_ = static_cast<TimelineEase>(Clamp(ease, 0, static_cast<int>(NameCount(TIMELINE_EASE_NAMES)) - 1));
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Easing function applied inside the in and out ramps of the clip.");
    }

    // Everything that only exists on one track type: assigned resources, blending, payload.
    RenderClipTypeFields(track);
}

// Resource field of a clip. The path is drawn as a selectable because ui::Text registers no item
// id, which would leave the drag&drop target without anything to arm against.
bool TimelineViewTab::EditResourceDrop(const char* label, const ea::string& path, StringHash objectType,
    ea::string& result)
{
    bool changed = false;
    ui::PushID(label);
    const ea::string display = path.empty()
        ? Format("{}: (none)", label)
        : Format("{}: {}", label, GetFileNameAndExtension(path).c_str());
    ui::Selectable(display.c_str(), false);
    if (ui::IsItemHovered() && !path.empty())
        ui::SetTooltip("%s", path.c_str());
    if (ui::BeginDragDropTargetCustom(ImRect(ui::GetItemRectMin(), ui::GetItemRectMax()),
            ui::GetID("resource_drop")))
    {
        const auto* payload = dynamic_cast<const ResourceDragDropPayload*>(DragDropPayload::Get());
        if (payload && payload->resources_.size() == 1 && payload->resources_[0].HasObjectType(objectType))
        {
            if (ui::AcceptDragDropPayload(DragDropPayloadType.c_str(), ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
            {
                // Highlight the field while a compatible resource hovers over it.
                ui::GetWindowDrawList()->AddRect(ui::GetItemRectMin(), ui::GetItemRectMax(),
                    IM_COL32(255, 200, 0, 255), 2.0f, 0, 2.0f);
                if (ui::GetDragDropPayload()->IsDelivery())
                {
                    result = payload->resources_[0].resourceName_;
                    changed = true;
                }
            }
        }
        ui::EndDragDropTarget();
    }
    ui::PopID();
    return changed;
}

// Editable table of a VariantMap. Every row names a key, its type and its value; the type combo
// rewrites the value of the row so a key never keeps a value of the type it was created with.
void TimelineViewTab::RenderVariantMapRows(VariantMap& data)
{
    static const VariantType variantTypes[] = {VAR_STRING, VAR_FLOAT, VAR_BOOL, VAR_INT, VAR_VECTOR3};
    static const char* const typeNames[] = {"String", "Float", "Bool", "Int", "Vector3", nullptr};

    if (data.empty())
        ui::TextDisabled("(no payload)");

    // The keys are snapshotted: renaming or deleting an entry while the table is drawn would
    // otherwise invalidate the iterator of the row below it. A hash only shows its name when the
    // string was registered with it, which deserialization of a map always does.
    ea::vector<StringHash> keys;
    for (const auto& entry : data)
        keys.push_back(entry.first);

    unsigned row = 0;
    for (const StringHash& key : keys)
    {
        ui::PushID(static_cast<int>(row));
        auto found = data.find(key);
        if (found == data.end())
        {
            ui::PopID();
            ++row;
            continue;
        }
        Variant& value = found->second;

        const ea::string name = key.Reverse().empty() ? key.ToString() : key.Reverse();
        char nameBuffer[128];
        snprintf(nameBuffer, sizeof(nameBuffer), "%s", name.c_str());
        const bool nameEdited = ui::InputText("##key", nameBuffer, sizeof(nameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue) && name != nameBuffer;
        ui::SameLine();
        int typeIndex = 0;
        for (unsigned i = 0; i < sizeof(variantTypes) / sizeof(variantTypes[0]); ++i)
        {
            if (value.GetType() == variantTypes[i])
            {
                typeIndex = static_cast<int>(i);
                break;
            }
        }
        ui::SetNextItemWidth(90.0f);
        if (ui::Combo("##type", &typeIndex, typeNames, static_cast<int>(NameCount(typeNames))))
        {
            BeginModify();
            value = Variant(variantTypes[typeIndex]);
        }
        ui::SameLine();
        switch (value.GetType())
        {
        case VAR_STRING:
        {
            ea::string text = value.ToString();
            if (EditString("##value", text))
                value = text;
            break;
        }
        case VAR_FLOAT:
        case VAR_DOUBLE:
        {
            float number = value.GetFloat();
            if (ui::InputFloat("##value", &number))
            {
                BeginModify();
                value = value.GetType() == VAR_DOUBLE ? Variant(static_cast<double>(number)) : Variant(number);
            }
            break;
        }
        case VAR_BOOL:
        {
            bool flag = value.GetBool();
            if (ui::Checkbox("##value", &flag))
            {
                BeginModify();
                value = flag;
            }
            break;
        }
        case VAR_INT:
        {
            int number = value.GetInt();
            if (ui::InputInt("##value", &number))
            {
                BeginModify();
                value = number;
            }
            break;
        }
        case VAR_VECTOR3:
        {
            // InputFloat3 writes through a float array, the components of a Vector3 are not
            // exposed as one.
            Vector3 vector = value.GetVector3();
            float components[3] = {vector.x_, vector.y_, vector.z_};
            if (ui::InputFloat3("##value", components))
            {
                BeginModify();
                value = Vector3(components[0], components[1], components[2]);
            }
            break;
        }
        default:
            // Types outside the table are shown but not editable: their text form is not
            // necessarily parseable, and silently dropping the value would lose data.
            ui::TextDisabled("%s", value.ToString().c_str());
            break;
        }

        ui::SameLine();
        if (ui::Button("Del"))
        {
            BeginModify();
            data.erase(key);
        }
        // A rename is applied after the row was drawn, so the value edited above survives it. It is
        // refused for a key whose name could not be recovered, because editing its hex display back
        // would create a second entry instead of renaming the first one.
        if (nameEdited && !key.Reverse().empty())
        {
            BeginModify();
            const Variant renamed = value;
            data.erase(key);
            data[StringHash(nameBuffer)] = renamed;
        }
        ui::PopID();
        ++row;
    }

    if (ui::Button("Add Payload Row"))
    {
        BeginModify();
        unsigned index = 1;
        ea::string name;
        do
            name = Format("key{}", index++);
        while (data.find(StringHash(name)) != data.end());
        data[StringHash(name)] = Variant(0.0f);
    }
}

// Fields that exist only on the clip type of the selected track, one line each.
void TimelineViewTab::RenderClipTypeFields(TimelineTrack* track)
{
    if (auto* animationTrack = dynamic_cast<TimelineAnimationTrack*>(track))
    {
        if (selectedClip_ >= animationTrack->clips_.size())
            return;
        TimelineAnimationClip& clip = animationTrack->clips_[selectedClip_];

        ea::string path = clip.animationRef_.name_;
        if (path.empty() && clip.animation_)
            path = clip.animation_->GetName();
        ea::string dropped;
        if (EditResourceDrop("Animation", path, Animation::GetTypeStatic(), dropped))
        {
            BeginModify();
            clip.animation_ = GetSubsystem<ResourceCache>()->GetResource<Animation>(dropped);
            clip.animationRef_ = GetResourceRef(clip.animation_, Animation::GetTypeStatic());
        }

        bool additive = clip.additive_;
        if (ui::Checkbox("Additive", &additive) && additive != clip.additive_)
        {
            BeginModify();
            clip.additive_ = additive;
        }
        ui::SameLine();
        ui::SetNextItemWidth(90.0f);
        int layer = static_cast<int>(clip.layer_);
        if (ui::DragInt("Layer", &layer, 1.0f, 0, 255) && layer != static_cast<int>(clip.layer_))
        {
            BeginModify();
            clip.layer_ = static_cast<unsigned>(Clamp(layer, 0, 255));
        }

        // The bone the animation is applied from. Free text because the track may be bound to any
        // model, but the bone tracks of the assigned animation are offered as the usual choice.
        EditString("Start Bone", clip.startBone_);
        ui::SameLine();
        if (clip.animation_)
        {
            ui::SetNextItemWidth(170.0f);
            if (ui::BeginCombo("##bones", clip.startBone_.empty() ? "(all)" : clip.startBone_.c_str()))
            {
                if (ui::Selectable("(all)", clip.startBone_.empty()))
                {
                    BeginModify();
                    clip.startBone_.clear();
                }
                // An animation track is keyed by the name of the node or attribute it drives.
                for (const auto& entry : clip.animation_->GetTracks())
                {
                    const ea::string bone = entry.first.Reverse();
                    if (bone.empty())
                        continue;
                    if (ui::Selectable(bone.c_str(), clip.startBone_ == bone))
                    {
                        BeginModify();
                        clip.startBone_ = bone;
                    }
                }
                ui::EndCombo();
            }
        }
    }
    else if (auto* audioTrack = dynamic_cast<TimelineAudioTrack*>(track))
    {
        if (selectedClip_ >= audioTrack->clips_.size())
            return;
        TimelineAudioClip& clip = audioTrack->clips_[selectedClip_];

        ea::string path = clip.soundRef_.name_;
        if (path.empty() && clip.sound_)
            path = clip.sound_->GetName();
        ea::string dropped;
        if (EditResourceDrop("Sound", path, Sound::GetTypeStatic(), dropped))
        {
            BeginModify();
            clip.sound_ = GetSubsystem<ResourceCache>()->GetResource<Sound>(dropped);
            clip.soundRef_ = GetResourceRef(clip.sound_, Sound::GetTypeStatic());
        }

        ui::SetNextItemWidth(170.0f);
        float volume = clip.volume_;
        if (ui::SliderFloat("Volume", &volume, 0.0f, 1.0f, "%.2f") && volume != clip.volume_)
        {
            BeginModify();
            clip.volume_ = Clamp(volume, 0.0f, 1.0f);
        }
        ui::SameLine();
        ui::SetNextItemWidth(170.0f);
        float pitch = clip.pitch_;
        if (ui::SliderFloat("Pitch", &pitch, 0.25f, 4.0f, "%.2f") && pitch != clip.pitch_)
        {
            BeginModify();
            clip.pitch_ = Clamp(pitch, 0.25f, 4.0f);
        }

        // A sound of known length gives the clip its natural duration; the speed of the clip is
        // taken into account so the block still ends where the sound ends.
        if (ui::Button("Fit Clip To Sound"))
        {
            if (clip.sound_ && clip.sound_->GetLength() > 0.0f)
            {
                BeginModify();
                clip.duration_ = Max(clip.sound_->GetLength() / Max(clip.timeScale_, 0.05f), MinClipDuration);
            }
            else
            {
                bindingError_ = "The clip has no sound with a known length";
                bindingErrorTime_ = ui::GetTime();
            }
        }
    }
    else if (auto* controlTrack = dynamic_cast<TimelineControlTrack*>(track))
    {
        if (selectedClip_ >= controlTrack->clips_.size())
            return;
        TimelineControlClip& clip = controlTrack->clips_[selectedClip_];

        ea::string dropped;
        if (EditResourceDrop("Prefab", clip.prefabRef_.name_, PrefabResource::GetTypeStatic(), dropped))
        {
            BeginModify();
            clip.prefab_ = GetSubsystem<ResourceCache>()->GetResource<PrefabResource>(dropped);
            clip.prefabRef_ = GetResourceRef(clip.prefab_, PrefabResource::GetTypeStatic());
        }
        if (EditResourceDrop("Nested Timeline", clip.timelineRef_.name_, TimelineResource::GetTypeStatic(),
                dropped))
        {
            BeginModify();
            clip.timeline_ = GetSubsystem<ResourceCache>()->GetResource<TimelineResource>(dropped);
            clip.timelineRef_ = GetResourceRef(clip.timeline_, TimelineResource::GetTypeStatic());
        }

        int postPlayback = static_cast<int>(clip.postPlayback_);
        ui::SetNextItemWidth(170.0f);
        if (ui::Combo("Post Playback", &postPlayback, TIMELINE_POST_PLAYBACK_NAMES,
                static_cast<int>(NameCount(TIMELINE_POST_PLAYBACK_NAMES))))
        {
            BeginModify();
            clip.postPlayback_ = static_cast<TimelinePostPlayback>(postPlayback);
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("What happens to the spawned object when the clip ends.");
    }
    else if (auto* activationTrack = dynamic_cast<TimelineActivationTrack*>(track))
    {
        if (selectedClip_ >= activationTrack->clips_.size())
            return;
        TimelineActivationClip& clip = activationTrack->clips_[selectedClip_];
        bool active = clip.active_;
        if (ui::Checkbox("Active", &active) && active != clip.active_)
        {
            BeginModify();
            clip.active_ = active;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Enabled state the bound node gets while this clip is active.");
    }
    else if (auto* signalTrack = dynamic_cast<TimelineSignalTrack*>(track))
    {
        if (selectedClip_ >= signalTrack->clips_.size())
            return;
        TimelineSignalClip& clip = signalTrack->clips_[selectedClip_];

        // The names already used anywhere in the timeline are the usual choice for a new signal:
        // receivers are matched by name, so a typo creates a signal nobody listens for.
        ea::vector<ea::string> names;
        ea::vector<TrackRow> allRows;
        FlattenTracks(timeline_.Get(), allRows);
        for (const TrackRow& trackRow : allRows)
        {
            auto* other = dynamic_cast<TimelineSignalTrack*>(trackRow.track_);
            if (!other)
                continue;
            for (const auto& otherClip : other->clips_)
            {
                if (otherClip.signal_.empty()
                    || ea::find(names.begin(), names.end(), otherClip.signal_) != names.end())
                    continue;
                names.push_back(otherClip.signal_);
            }
        }
        EditString("Signal", clip.signal_);
        if (!names.empty())
        {
            ui::SameLine();
            ui::SetNextItemWidth(170.0f);
            if (ui::BeginCombo("##signalNames", "Known signals..."))
            {
                for (const ea::string& name : names)
                {
                    if (ui::Selectable(name.c_str(), clip.signal_ == name))
                    {
                        BeginModify();
                        clip.signal_ = name;
                    }
                }
                ui::EndCombo();
            }
        }

        bool emitOnce = clip.emitOnce_;
        if (ui::Checkbox("Emit Once", &emitOnce) && emitOnce != clip.emitOnce_)
        {
            BeginModify();
            clip.emitOnce_ = emitOnce;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Fire the signal only the first time playback crosses the clip.");
        ui::SameLine();
        bool retroactive = clip.retroactive_;
        if (ui::Checkbox("Retroactive", &retroactive) && retroactive != clip.retroactive_)
        {
            BeginModify();
            clip.retroactive_ = retroactive;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Also fire the signal when playback starts past its time.");

        ui::TextDisabled("Payload");
        RenderVariantMapRows(clip.data_);
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
        ui::TextDisabled("Key");
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
    ui::Combo("##newCurveType", &newCurveType_, curveTypeNames, static_cast<int>(kNumCurveTypes));
    ui::SameLine();
    if (ui::Button("Add Curve"))
    {
        const ea::string curveName = newCurveName_;
        if (!curveName.empty() && newCurveType_ >= 0 && newCurveType_ < static_cast<int>(kNumCurveTypes))
        {
            BeginModify();
            AddPropertyCurve(propertyClip, curveName, curveTypes[newCurveType_]);
            newCurveName_[0] = '\0';
        }
    }

    // The canvas itself lives in the standalone "Timeline Curve Editor" window; this only
    // toggles it for the selected clip.
    if (ui::Button(curveEditor_.IsOpen() ? "Close Curve Editor" : "Open Curve Editor"))
    {
        if (curveEditor_.IsOpen())
            curveEditor_.Close();
        else
            curveEditor_.Open();
    }
}

}
