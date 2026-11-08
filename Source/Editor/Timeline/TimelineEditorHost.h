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

namespace Urho3D
{

/// Services a floating Timeline editing window needs from the tab that owns it. The windows
/// themselves never touch the undo stack or the play head: they ask the host instead, which
/// keeps them independent of the tab implementation.
struct TimelineEditorHost
{
    virtual ~TimelineEditorHost() = default;

    /// Start an undoable modification of the displayed timeline. No-op if one is in progress.
    virtual void BeginModify() = 0;
    /// Play head position in timeline seconds.
    virtual float GetCursorTime() const = 0;
    /// Clip-local time of the selected key row, or a negative value when no key is selected.
    virtual float GetSelectedKeyTime() const = 0;
    /// Select the key row at the clip-local time, or clear the selection when negative.
    virtual void SetSelectedKeyTime(float localTime) = 0;
};

}
