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

#pragma once

#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Math/MathDefs.h>
#include <Urho3D/SystemUI/SystemUI.h>

#include <EASTL/algorithm.h>
#include <EASTL/vector.h>

#include <cmath>

namespace Urho3D
{

// Shared look and feel of the Timeline editor widgets. The colors are used both by the sequencer
// tab and by the standalone curve / dope sheet windows, so they live in one place.
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
// Rubber band of a marquee selection. Both floating canvases, the curve editor and the dope sheet,
// draw the same rectangle so that the gesture looks the same everywhere in the editor.
const ImU32 ColorSelectionRect = IM_COL32(255, 200, 60, 34);
const ImU32 ColorSelectionBorder = IM_COL32(255, 200, 60, 190);
// Halo behind a selected key, which keeps the selection readable over a dark canvas.
const ImU32 ColorSelectionGlow = IM_COL32(255, 200, 60, 255);

/// Replace the alpha channel of an ImU32 color.
inline ImU32 WithAlpha(ImU32 color, unsigned alpha)
{
    return (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, alpha);
}

/// Tint of a track type in every canvas of the Timeline editor: the rows of the sequencer, the
/// blocks of the dope sheet and the color bar of a header all use the same color for the same type.
/// The dispatch works on the type name, so the helper needs none of the track headers.
inline ImU32 GetTrackTypeColor(const ea::string& typeName)
{
    if (typeName == "Animation")
        return IM_COL32(64, 148, 148, 255);
    if (typeName == "Control")
        return IM_COL32(190, 115, 40, 255);
    if (typeName == "Activation")
        return IM_COL32(96, 150, 96, 255);
    if (typeName == "Audio")
        return IM_COL32(72, 110, 158, 255);
    if (typeName == "Marker")
        return IM_COL32(170, 140, 30, 255);
    if (typeName == "Signal")
        return IM_COL32(158, 72, 72, 255);
    if (typeName == "Property")
        return IM_COL32(130, 100, 170, 255);
    return IM_COL32(110, 110, 110, 255); // Group track
}

/// Human readable seconds with a precision that follows the magnitude.
inline ea::string FormatTime(float time)
{
    if (time >= 100.0f - 0.001f)
        return Format("{:.0f}s", time);
    if (time >= 10.0f - 0.001f)
        return Format("{:.1f}s", time);
    return Format("{:.2f}s", time);
}

// Time snapping step shared by the sequencer and the curve canvas.
constexpr float SnapStep = 0.05f;

/// Round the time to the snap step, unless snapping is suppressed (held down Ctrl).
inline float SnapTime(float time, bool snapEnabled)
{
    if (!snapEnabled)
        return time;
    return floorf(time / SnapStep + 0.5f) * SnapStep;
}

/// Display unit of the Timeline editor. Frame numbers are only meaningful together with the frame
/// rate stored in the resource, so every helper that works in frames takes the rate as well.
enum class TimelineTimeUnit
{
    Seconds,
    Frames,
};

/// Human readable time in the given unit. Frame numbers are integral by definition, while the
/// seconds precision follows the magnitude of the value.
inline ea::string FormatTime(float time, TimelineTimeUnit unit, float frameRate)
{
    if (unit == TimelineTimeUnit::Frames)
        return Format("f{}", lroundf(time * Max(frameRate, 1.0f)));
    return FormatTime(time);
}

/// Tick steps of the time ruler for the given zoom. The seconds mode uses round second values, the
/// frames mode whole frame counts, so the labels of either mode stay readable at every zoom level
/// and the frames mode never prints a fraction.
inline void ChooseRulerSteps(float pixelsPerSecond, float frameRate, TimelineTimeUnit unit,
    float minMajorPx, float minMinorPx, float& majorStep, float& minorStep)
{
    static const float secondSteps[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f,
        60.0f};
    static const int frameSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600, 7200};
    const float secPerFrame = 1.0f / Max(frameRate, 1.0f);

    if (unit == TimelineTimeUnit::Frames)
    {
        const int numFrameSteps = static_cast<int>(sizeof(frameSteps) / sizeof(frameSteps[0]));
        int majorIdx = numFrameSteps - 1;
        for (int i = 0; i < numFrameSteps; ++i)
        {
            if (frameSteps[i] * secPerFrame * pixelsPerSecond >= minMajorPx)
            {
                majorIdx = i;
                break;
            }
        }
        int minorIdx = majorIdx;
        for (int i = 0; i < numFrameSteps; ++i)
        {
            if (frameSteps[i] * secPerFrame * pixelsPerSecond >= minMinorPx)
            {
                minorIdx = Min(i, majorIdx);
                break;
            }
        }
        majorStep = frameSteps[majorIdx] * secPerFrame;
        minorStep = frameSteps[minorIdx] * secPerFrame;
        return;
    }

    const int numSecondSteps = static_cast<int>(sizeof(secondSteps) / sizeof(secondSteps[0]));
    int majorIdx = numSecondSteps - 1;
    for (int i = 0; i < numSecondSteps; ++i)
    {
        if (secondSteps[i] * pixelsPerSecond >= minMajorPx)
        {
            majorIdx = i;
            break;
        }
    }
    majorStep = secondSteps[majorIdx];
    minorStep = majorStep / 5.0f;
}

/// Snap the time to the grid of the display unit and, when one of the sorted «candidates» lies
/// closer than «tolerance» seconds, to that candidate instead. Nothing is snapped while
/// «snapEnabled» is false, which is how holding Ctrl frees an edit from the grid.
inline float SnapTimeTo(float time, bool snapEnabled, TimelineTimeUnit unit, float frameRate,
    const ea::vector<float>& candidates, float tolerance)
{
    if (!snapEnabled)
        return time;

    float snapped = SnapTime(time, true);
    if (unit == TimelineTimeUnit::Frames)
    {
        const float secPerFrame = 1.0f / Max(frameRate, 1.0f);
        snapped = floorf(time / secPerFrame + 0.5f) * secPerFrame;
    }

    // A candidate closer than the grid wins, which is what turns clip edges and keyframes into
    // snap points. The list is sorted, so a binary search bounds the scan to the candidates that
    // are near enough to matter.
    const auto first = ea::lower_bound(candidates.begin(), candidates.end(), time - tolerance);
    const auto last = ea::upper_bound(first, candidates.end(), time + tolerance);
    float bestDelta = Min(Abs(time - snapped), tolerance);
    for (auto it = first; it != last; ++it)
    {
        const float delta = Abs(time - *it);
        if (delta < bestDelta)
        {
            bestDelta = delta;
            snapped = *it;
        }
    }
    return snapped;
}

}
