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

#include <Urho3D/Core/Variant.h>
#include <Urho3D/SystemUI/SystemUI.h>
#include <Urho3D/Timeline/TimelinePropertyTrack.h>

#include <EASTL/vector.h>

namespace Urho3D
{

// Data layer helpers shared by the sequencer tab, the curve editor and the dope sheet.
// They only touch the Timeline resource contents, so they never start undo actions and
// never depend on the widget state.

/// Variant types offered when a new property curve is created, and their display names.
inline const char* curveTypeNames[] = { "Float", "Double", "Int", "Bool", "Vector2", "Vector3", "Vector4", "Color",
    "Quaternion" };
inline const VariantType curveTypes[] = { VAR_FLOAT, VAR_DOUBLE, VAR_INT, VAR_BOOL, VAR_VECTOR2, VAR_VECTOR3,
    VAR_VECTOR4, VAR_COLOR, VAR_QUATERNION };
inline constexpr unsigned kNumCurveTypes = sizeof(curveTypes) / sizeof(curveTypes[0]);

/// Append a curve with a single key at the clip start holding the default value of the type.
void AddPropertyCurve(TimelinePropertyClip& clip, const ea::string& name, VariantType type);

// Keys of all curves of a clip are expected to be aligned in time, so they are displayed and
// edited as a single set of time points.

/// Sorted distinct key times of every curve of the clip.
ea::vector<float> GetPropertyKeyTimes(const TimelinePropertyClip& clip);
/// Whether any curve of the clip already holds a key at the given clip-local time. Two times
/// within a tenth of a millisecond of each other are the same row, because that is the distance at
/// which a curve with two keys at once stops being predictable.
bool HasPropertyKeyRowAt(const TimelinePropertyClip& clip, float localTime);
/// Insert a key at the given clip-local time in every curve, copying the value of the nearest key.
/// Does nothing while a key row already sits at that time.
void AddPropertyKeyAt(TimelinePropertyClip& clip, float localTime);
/// Move the whole key row from one clip-local time to another, keeping the curves sorted.
void MovePropertyKeysAt(TimelinePropertyClip& clip, float fromLocal, float toLocal);
/// Delete the key row at the given clip-local time from every curve.
void RemovePropertyKeysAt(TimelinePropertyClip& clip, float localTime);

// Multi-component values are shown one component at a time, matching how the runtime
// interpolates them (scalar tangents apply per component in practice).

/// Number of displayable components of a curve value type. Zero means the type has no numeric
/// representation and cannot be drawn as a curve.
int GetCurveComponentCount(VariantType type);
/// Read one component of a curve key value as a float.
float GetCurveComponentValue(const Variant& value, int component);
/// Write one component of a curve key value.
void SetCurveComponentValue(Variant& value, int component, float newValue);
/// Sample a cubic Hermite segment, mirroring the runtime evaluation of TimelinePropertyTrack.
float HermiteScalarCurve(float p0, float p1, float m0, float m1, float t, float span);
/// Sample one component of the curve at the given clip-local time, following the same
/// interpolation rules as the runtime so the canvas matches playback.
float EvaluateCurveComponent(const TimelinePropertyCurve& curve, float time, int component);
/// Per-curve color of the canvases, cycled when a clip has more curves than palette entries.
ImU32 GetCurveCanvasColor(unsigned index);

}
