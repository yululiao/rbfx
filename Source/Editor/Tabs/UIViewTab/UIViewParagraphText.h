//
// Paragraph (multi-line text) logic for the UI layout editor's structural
// Content editing.
//
// Model contract: an editable paragraph is an element whose children are text
// runs and bare <br/> only. The editor buffer shows one line per run; a visible
// line break IS a <br/> (under white-space: normal a raw \n collapses to a
// space). Editing therefore rewrites lines <-> runs/<br/> children: the commit
// reuses every run/break whose line survives (its bytes and spine anchor stay
// untouched) and rebuilds only the changed middle.
//
// Deliberately std/RmlTextModel only (like RmlTextModel.h) so the planner and
// the patch math unit-test headless; the editor layer only converts UiNodes in
// and applies the returned patches to the spine.
//
#pragma once

#include <string>
#include <vector>

#include "RmlTextModel.h"

/// One child of a paragraph as the planner and the emitter see it.
struct ParagraphChildState
{
    int srcNode = -1;    ///< spine anchor; -1 for editor-created children
    bool isText = false; ///< text run; false is a <br/> (the only other kind)
    std::string text;    ///< text runs: the current (verbatim) model text
    std::string markup;  ///< editor-created children only: bytes to splice
};

/// One child slot of the rebuilt paragraph, decided by PlanParagraphChildren.
struct ParagraphPlanEntry
{
    int reuseChild = -1; ///< keep oldChildren[reuseChild] (anchor + bytes); -1 = editor-created
    bool isBreak = false;///< editor-created <br/> (when reuseChild < 0 and !isBreak: a text run)
    std::string text;    ///< editor-created text run contents (already normalized)
};

/// white-space: normal line semantics: whitespace runs collapse to one space,
/// leading/trailing whitespace is dropped.
std::string NormalizeParagraphLine(const std::string& text);

/// Split a multi-line editor buffer on '\n'. Always returns at least one line.
std::vector<std::string> SplitParagraphLines(const std::string& buffer);

/// SplitParagraphLines + NormalizeParagraphLine - the editor's canonical lines.
std::vector<std::string> NormalizedParagraphLines(const std::string& buffer);

/// Inverse of SplitParagraphLines: join with '\n' (no trailing break).
std::string JoinParagraphLines(const std::vector<std::string>& lines);

/// Longest common prefix / suffix of two normalized line lists (non-overlapping).
void ComputeParagraphEdges(const std::vector<std::string>& oldLines,
    const std::vector<std::string>& newLines, int& prefixLen, int& suffixLen);

/// The editor's lines (normalized) for a paragraph's existing children; false
/// when the children are not the editable shape (e.g. two adjacent text runs
/// around a comment).
bool ParagraphLinesOfChildren(const std::vector<ParagraphChildState>& children,
    std::vector<std::string>& lines);

/// Rebuild plan for \a newLines (normalized) over \a oldChildren: runs and
/// breaks whose lines survive are reused verbatim, the replaced middle is
/// minted fresh (a fresh <br/> per separator whose source break is dropped).
/// False when \a oldChildren cannot be indexed - callers then rebuild
/// wholesale with FreshParagraphPlan.
bool PlanParagraphChildren(const std::vector<ParagraphChildState>& oldChildren,
    const std::vector<std::string>& newLines, std::vector<ParagraphPlanEntry>& out);

/// All-fresh plan for \a newLines (normalized): no reuse, every child minted.
std::vector<ParagraphPlanEntry> FreshParagraphPlan(const std::vector<std::string>& newLines);

/// Save-time patches for a paragraph whose children a command rebuilt: removal
/// patches for dropped anchored children, in-place text patches for changed
/// runs, and one positional insert per editor-created child run (inserted
/// before the next anchored sibling, or at the inner end). Whitespace spine
/// runs and comments are never touched, so file layout survives.
bool ComputeParagraphChildrenPatches(const RmlTextModel& src, int hostSrcIdx,
    const std::vector<ParagraphChildState>& children, std::vector<RmlPatch>& out);
