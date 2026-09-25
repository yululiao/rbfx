//
// See UIViewParagraphText.h for the design contract.
//

#include "UIViewParagraphText.h"

namespace
{

bool IsSpaceChar(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

/// Meaningful text: carries visible content. Whitespace-only runs are never
/// modeled by the editor tree, so a text run here is always meaningful; the
/// check guards the removal pass against exotic input.
bool IsMeaningfulTextRun(const std::string& text)
{
    for (char c : text)
    {
        if (!IsSpaceChar(c))
            return true;
    }
    return false;
}

} // namespace

std::string NormalizeParagraphLine(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (char c : text)
    {
        if (IsSpaceChar(c))
        {
            if (!out.empty())
                pendingSpace = true;
            continue;
        }
        if (pendingSpace)
        {
            out += ' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

std::vector<std::string> SplitParagraphLines(const std::string& buffer)
{
    std::vector<std::string> lines;
    std::string current;
    for (char c : buffer)
    {
        if (c == '\n')
        {
            lines.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    lines.push_back(current);
    return lines;
}

std::vector<std::string> NormalizedParagraphLines(const std::string& buffer)
{
    std::vector<std::string> lines = SplitParagraphLines(buffer);
    for (std::string& line : lines)
        line = NormalizeParagraphLine(line);
    return lines;
}

std::string JoinParagraphLines(const std::vector<std::string>& lines)
{
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            out += '\n';
        out += lines[i];
    }
    return out;
}

void ComputeParagraphEdges(const std::vector<std::string>& oldLines,
    const std::vector<std::string>& newLines, int& prefixLen, int& suffixLen)
{
    const int nOld = static_cast<int>(oldLines.size());
    const int nNew = static_cast<int>(newLines.size());

    prefixLen = 0;
    while (prefixLen < nOld && prefixLen < nNew && oldLines[prefixLen] == newLines[prefixLen])
        ++prefixLen;

    suffixLen = 0;
    while (suffixLen < nOld - prefixLen && suffixLen < nNew - prefixLen
        && oldLines[nOld - 1 - suffixLen] == newLines[nNew - 1 - suffixLen])
    {
        ++suffixLen;
    }
}

bool ParagraphLinesOfChildren(const std::vector<ParagraphChildState>& children,
    std::vector<std::string>& lines)
{
    lines.clear();
    std::string current;
    bool lineHasRun = false;
    for (const ParagraphChildState& child : children)
    {
        if (!child.isText)
        {
            lines.push_back(NormalizeParagraphLine(current));
            current.clear();
            lineHasRun = false;
        }
        else
        {
            if (lineHasRun)
                return false; // two adjacent text runs: a comment sits between them
            current = child.text;
            lineHasRun = true;
        }
    }
    lines.push_back(NormalizeParagraphLine(current));
    return true;
}

bool PlanParagraphChildren(const std::vector<ParagraphChildState>& oldChildren,
    const std::vector<std::string>& newLines, std::vector<ParagraphPlanEntry>& out)
{
    out.clear();

    std::vector<std::string> oldLines;
    if (!ParagraphLinesOfChildren(oldChildren, oldLines))
        return false;

    const int nOld = static_cast<int>(oldLines.size());
    const int nNew = static_cast<int>(newLines.size());
    if (nNew <= 0)
        return false;

    // Index the old children by line: the run of line k (or -1 for an empty
    // line) and the break that closes line k.
    std::vector<int> lineRuns(nOld, -1);
    std::vector<int> lineSeps(nOld, -1);
    {
        int line = 0;
        for (int i = 0; i < static_cast<int>(oldChildren.size()); ++i)
        {
            if (!oldChildren[i].isText)
            {
                lineSeps[line] = i;
                ++line;
            }
            else
            {
                lineRuns[line] = i;
            }
        }
    }

    int prefix = 0;
    int suffix = 0;
    ComputeParagraphEdges(oldLines, newLines, prefix, suffix);
    const int middle = nNew - prefix - suffix;
    const int lineShift = nNew - nOld; // old line index = new index - lineShift in the suffix zone

    for (int k = 0; k < nNew; ++k)
    {
        // The run of line k (empty lines carry none).
        if (!newLines[k].empty())
        {
            int reuse = -1;
            if (k < prefix)
            {
                reuse = lineRuns[k];
            }
            else if (suffix > 0 && k >= nNew - suffix)
            {
                const int oldK = k - lineShift;
                if (oldK >= 0 && oldK < nOld)
                    reuse = lineRuns[oldK];
            }
            ParagraphPlanEntry entry;
            entry.reuseChild = reuse;
            entry.isBreak = false;
            if (reuse < 0)
                entry.text = newLines[k];
            out.push_back(entry);
        }

        // The separator after line k.
        if (k < nNew - 1)
        {
            int reuseSep = -1;
            if (k <= prefix - 2)
            {
                reuseSep = lineSeps[k];
            }
            else if (suffix > 0 && k >= nNew - suffix)
            {
                const int oldK = k - lineShift;
                if (oldK >= 0 && oldK < nOld - 1)
                    reuseSep = lineSeps[oldK];
            }
            else if (middle == 0 && k == prefix - 1 && prefix > 0 && suffix > 0)
            {
                // Deleting whole lines joins the surviving ones: keep the break
                // that closed the last surviving prefix line as the join.
                reuseSep = lineSeps[k];
            }
            ParagraphPlanEntry entry;
            entry.reuseChild = reuseSep;
            entry.isBreak = true;
            out.push_back(entry);
        }
    }

    if (out.empty())
    {
        // Emptying every line leaves one <br/> so the paragraph stays editable.
        ParagraphPlanEntry entry;
        entry.reuseChild = -1;
        entry.isBreak = true;
        out.push_back(entry);
    }
    return true;
}

std::vector<ParagraphPlanEntry> FreshParagraphPlan(const std::vector<std::string>& newLines)
{
    std::vector<ParagraphPlanEntry> out;
    const int nNew = static_cast<int>(newLines.size());
    for (int k = 0; k < nNew; ++k)
    {
        if (!newLines[k].empty())
        {
            ParagraphPlanEntry entry;
            entry.reuseChild = -1;
            entry.isBreak = false;
            entry.text = newLines[k];
            out.push_back(entry);
        }
        if (k < nNew - 1)
        {
            ParagraphPlanEntry entry;
            entry.reuseChild = -1;
            entry.isBreak = true;
            out.push_back(entry);
        }
    }
    if (out.empty())
    {
        ParagraphPlanEntry entry;
        entry.reuseChild = -1;
        entry.isBreak = true;
        out.push_back(entry);
    }
    return out;
}

bool ComputeParagraphChildrenPatches(const RmlTextModel& src, int hostSrcIdx,
    const std::vector<ParagraphChildState>& children, std::vector<RmlPatch>& out)
{
    if (hostSrcIdx < 0 || hostSrcIdx >= src.NodeCount())
        return false;
    const RmlNode& host = src.Node(hostSrcIdx);
    if (host.kind != RmlNodeKind::Element)
        return false;

    const auto referenced = [&children](int sidx)
    {
        for (const ParagraphChildState& child : children)
        {
            if (child.srcNode == sidx)
                return true;
        }
        return false;
    };

    // Dropped anchored children: patch their bytes out. Whitespace runs and
    // comments (never modeled) are untouched, so original layout survives.
    for (int cidx : host.children)
    {
        const RmlNode& cn = src.Node(cidx);
        if (referenced(cidx))
            continue;
        RmlPatch patch;
        if (cn.kind == RmlNodeKind::Element && src.ComputeElementRemovalPatch(cidx, patch))
            out.push_back(patch);
        else if (cn.kind == RmlNodeKind::Text && IsMeaningfulTextRun(cn.text)
            && src.ComputeTextPatch(cidx, "", patch))
            out.push_back(patch);
    }

    // Reused runs whose text changed: in-place patches.
    for (const ParagraphChildState& child : children)
    {
        if (child.srcNode < 0 || !child.isText)
            continue;
        const RmlNode& cn = src.Node(child.srcNode);
        if (cn.kind == RmlNodeKind::Text && cn.text != child.text)
        {
            RmlPatch patch;
            if (src.ComputeTextPatch(child.srcNode, child.text, patch))
                out.push_back(patch);
        }
    }

    // Editor-created children: one splice per consecutive run of them, placed
    // right before the next anchored sibling (or at the inner end).
    size_t i = 0;
    while (i < children.size())
    {
        if (children[i].srcNode >= 0)
        {
            ++i;
            continue;
        }
        size_t j = i;
        std::string markup;
        while (j < children.size() && children[j].srcNode < 0)
        {
            markup += children[j].markup;
            ++j;
        }
        int offset = host.innerSpan.End();
        for (size_t k = j; k < children.size(); ++k)
        {
            if (children[k].srcNode >= 0)
            {
                const RmlNode& next = src.Node(children[k].srcNode);
                offset = next.kind == RmlNodeKind::Text ? next.span.offset : next.whole.offset;
                break;
            }
        }
        RmlPatch patch;
        patch.span.offset = offset;
        patch.span.length = 0;
        patch.replacement = markup;
        out.push_back(patch);
        i = j;
    }
    return true;
}
