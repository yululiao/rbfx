//
// Copyright (c) 2017-2020 the rbfx project.
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

#include <Urho3D/SystemUI/SystemUI.h>

#include <EASTL/optional.h>
#include <EASTL/string.h>

namespace Urho3D
{

/// What a path field's browse button picks from the native OS dialog.
enum class PathFieldKind
{
    /// The dialog browses directories.
    Directory,
    /// The dialog browses files.
    File,
};

/// Open the native OS picker without any UI of its own: a directory when 'pickDirectory', else a
/// file of types matching 'filter' (an NFD extension spec, e.g. "png,jpg"; empty lists every
/// file). The dialog starts at 'initialDir' when that points somewhere on disk. Returns the
/// chosen path (absolute, forward slashes), or nullopt when the user cancelled or the dialog
/// failed. Lives in the editor because nfd only exists in desktop editor builds.
ea::optional<ea::string> PickNativePath(bool pickDirectory, const ea::string& filter,
    const ea::string& initialDir);

/// Render a folder-open button on the same line as the path field rendered before it. Clicking it
/// opens a native OS picker and, on selection, writes the choice into 'path' (absolute, forward
/// slashes). The dialog starts where the current value points, when it points somewhere.
///
/// 'filter' selects what is picked: null chooses a directory, "" a file of any type, and an NFD
/// extension spec ("png,jpg") a file of those types. Returns whether the field changed, so callers
/// can feed the result straight into their dirty tracking.
bool BrowseButton(const char* label, ea::string& path, const char* filter);

/// Render a whole path field as one unit: a text input, a browse button opening the native OS
/// picker (directories or files, per 'kind'), and a reveal button that opens the OS file manager
/// at the current value (disabled while the value points nowhere on disk). 'filter' is an NFD
/// extension spec ("png,jpg") narrowing the file dialog; null or empty lists every file.
/// 'tooltip', when given, documents the field on hover. Returns whether the path changed, either
/// by typing or by picking, so callers can feed the result straight into their dirty tracking.
bool PathField(const char* label, ea::string& path, PathFieldKind kind,
    const char* filter = nullptr, const char* tooltip = nullptr);

}
