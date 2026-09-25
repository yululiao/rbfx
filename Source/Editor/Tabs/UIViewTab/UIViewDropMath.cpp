//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewDropMath.h"

namespace Urho3D
{

namespace
{
float Axis(const Vector2& v, DropAxis axis)
{
    return axis == DropAxis::Horizontal ? v.x_ : v.y_;
}
} // namespace

DropSolution SolveFlowDrop(const Vector2& pointer, const DropTargetFacts& target)
{
    DropSolution solution;
    const Vector2 size = target.rectMax_ - target.rectMin_;
    const float extent = Axis(size, target.childAxis_);
    const float lo = Axis(target.rectMin_, target.childAxis_);
    const float p = Axis(pointer, target.childAxis_);

    if (target.hostable_)
    {
        // Middle half of the candidate: drop inside. A degenerate box and the
        // document root (nothing to sit beside) always read as inside.
        const float rel = extent > 0.0f ? (p - lo) / extent : 0.5f;
        if (target.forceInside_ || (rel >= 0.25f && rel <= 0.75f))
        {
            solution.kind_ = DropSolution::Kind::Inside;
            // The first child whose center sits past the pointer opens the
            // slot; past every child the drop appends at the end.
            solution.index_ = target.childCount_;
            for (const DropChild& child : target.children_)
            {
                if (child.center_ > p)
                {
                    solution.index_ = child.index_;
                    break;
                }
            }
            return solution;
        }
    }

    // Outer halves: before or after the candidate itself, by which side of
    // its center (on the sibling axis) the pointer sits on.
    const float center = Axis(target.rectMin_ + size * 0.5f, target.siblingAxis_);
    solution.kind_ = Axis(pointer, target.siblingAxis_) < center
        ? DropSolution::Kind::Before
        : DropSolution::Kind::After;
    solution.index_ = solution.kind_ == DropSolution::Kind::Before
        ? target.selfIndex_
        : target.selfIndex_ + 1;
    return solution;
}

bool IsFlowContainerTag(const ea::string& tag)
{
    // Block-level containers of RmlUi's default stylesheet plus the semantic
    // HTML wrappers authors treat as layout boxes. Only consulted for EMPTY
    // elements: a node with element children hosts a drop regardless of tag.
    static const char* const kContainers[] = {
        "div", "section", "article", "aside", "main", "nav", "header", "footer",
        "form", "ul", "ol", "li", "body",
    };
    for (const char* candidate : kContainers)
    {
        if (tag == candidate)
            return true;
    }
    return false;
}

}
