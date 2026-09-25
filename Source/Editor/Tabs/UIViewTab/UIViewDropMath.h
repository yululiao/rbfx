//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include <Urho3D/Math/Vector2.h>

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace Urho3D
{

// Pure drop-solving logic for the UI layout editor's structural (flow) drags.
// Deliberately free of any RmlUi / ImGui dependency, like UIViewLayoutMath:
// the controller feeds it the document-space geometry of one hit-test
// candidate and turns the returned slot back into a MoveNode / AddWidget call.

/// Flow direction of one line of children inside a container.
enum class DropAxis
{
    Vertical,
    Horizontal
};

/// One droppable child of a candidate container: the position of its box
/// center on the container's child axis, and its index in the model's FULL
/// child list (text nodes included - that is the index a move/insert takes).
struct DropChild
{
    float center_ = 0.0f;
    unsigned index_ = 0;
};

/// What the solver needs to know about one hit-test candidate.
struct DropTargetFacts
{
    /// The candidate's border box in document space.
    Vector2 rectMin_;
    Vector2 rectMax_;
    /// Flow axis of the candidate's children (how Inside slots are ordered).
    DropAxis childAxis_ = DropAxis::Vertical;
    /// Flow axis of the candidate's siblings inside its parent (how a drop
    /// beside the candidate is read).
    DropAxis siblingAxis_ = DropAxis::Vertical;
    /// The candidate may take children (container tag or already has some).
    bool hostable_ = false;
    /// The candidate has no parent slot to sit beside (the document root):
    /// the whole rect must read as the inside band.
    bool forceInside_ = false;
    /// The candidate's own index in its parent's full child list.
    unsigned selfIndex_ = 0;
    /// Its parent's full child count (where Inside appends).
    unsigned childCount_ = 0;
    /// Element children of the candidate, in model order.
    ea::vector<DropChild> children_;
};

/// Where a pointer drops relative to one candidate.
struct DropSolution
{
    enum class Kind
    {
        Invalid, ///< nothing sensible under this pointer
        Before, ///< insert right before the candidate, at index_
        After, ///< insert right after the candidate, at index_
        Inside, ///< insert into the candidate at child index_
    };

    Kind kind_ = Kind::Invalid;
    /// Insertion index into the target parent's FULL child list (text nodes
    /// included), in the pre-removal numbering MoveNode / AddWidget expect.
    unsigned index_ = 0;
};

/// Solve the drop of a pointer at \a pointer (document px) against one
/// candidate. The middle half of the candidate on its child axis reads as
/// Inside (when it can host); the outer halves read as Before / After the
/// candidate itself, decided by which side of its center (on the sibling
/// axis) the pointer sits on.
DropSolution SolveFlowDrop(const Vector2& pointer, const DropTargetFacts& target);

/// Tags that make sense as flow containers even while empty - the set the
/// editor is willing to drop "inside" of. Containers already holding element
/// children qualify regardless of tag.
bool IsFlowContainerTag(const ea::string& tag);

}
