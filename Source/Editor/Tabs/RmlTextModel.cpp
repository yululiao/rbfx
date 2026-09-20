//
// Implementation of the pure-text RML source model. See RmlTextModel.h for the contract.
//

#include "RmlTextModel.h"

#include <algorithm>
#include <cctype>
#include <functional>

namespace
{

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
bool IsNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool IsNameChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == ':' || c == '.';
}

std::string Lower(const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool IEquals(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }

bool IsAttrNameEnd(char c) { return IsSpace(c) || c == '=' || c == '>' || c == '/'; }

} // namespace

bool RmlTextModel::Load(const std::string& text)
{
    text_ = text;
    Parse();
    DetectStyle();
    return true;
}

// Case-insensitive find of a lowercase needle within text_, from 'from'; returns -1.
static int FindLower(const std::string& hay, const std::string& lowerNeedle, int from)
{
    const int n = static_cast<int>(hay.size());
    const int m = static_cast<int>(lowerNeedle.size());
    for (int k = from; k + m <= n; ++k)
    {
        if (Lower(hay.substr(k, m)) == lowerNeedle)
            return k;
    }
    return -1;
}

void RmlTextModel::Parse()
{
    nodes_.clear();
    RmlNode root;
    root.kind = RmlNodeKind::Root;
    root.parent = -1;
    nodes_.push_back(root);

    std::vector<int> stack; // open element node indices; stack.back() is the current parent
    stack.push_back(0);

    const int n = static_cast<int>(text_.size());
    int pos = 0;

    auto addLeaf = [&](RmlNodeKind kind, int start, int end, const std::string& content)
    {
        if (end <= start)
            return;
        RmlNode node;
        node.kind = kind;
        node.parent = stack.back();
        node.span.offset = start;
        node.span.length = end - start;
        node.text = content;
        const int parentIdx = stack.back();
        const int newIdx = static_cast<int>(nodes_.size());
        nodes_[parentIdx].children.push_back(newIdx);
        nodes_.push_back(node);
    };

    auto parseAttribute = [&](int it, std::vector<RmlAttribute>& attrs) -> int
    {
        const int nameStart = it;
        while (it < n && !IsAttrNameEnd(text_[it]))
            ++it;
        const int nameEnd = it;
        if (nameEnd == nameStart)
        {
            ++it; // guarantee progress on a stray char
            return it;
        }

        RmlAttribute attr;
        attr.name = text_.substr(nameStart, nameEnd - nameStart);

        int ws = it;
        while (ws < n && IsSpace(text_[ws]))
            ++ws;

        if (ws < n && text_[ws] == '=')
        {
            ++ws;
            while (ws < n && IsSpace(text_[ws]))
                ++ws;
            const int valueStart = ws;
            if (ws < n && (text_[ws] == '"' || text_[ws] == '\''))
            {
                const char q = text_[ws];
                ++ws;
                const int inner = ws;
                while (ws < n && text_[ws] != q)
                    ++ws;
                const int innerEnd = ws;
                if (ws < n)
                    ++ws; // consume closing quote
                attr.quote = q;
                attr.value = text_.substr(inner, innerEnd - inner);
                attr.valueSpan.offset = inner;
                attr.valueSpan.length = innerEnd - inner;
                attr.valueWithQuotes.offset = valueStart;
                attr.valueWithQuotes.length = ws - valueStart;
            }
            else
            {
                const int inner = ws;
                while (ws < n && !IsSpace(text_[ws]) && text_[ws] != '>')
                    ++ws;
                attr.quote = 0;
                attr.value = text_.substr(inner, ws - inner);
                attr.valueSpan.offset = inner;
                attr.valueSpan.length = ws - inner;
                attr.valueWithQuotes.offset = inner;
                attr.valueWithQuotes.length = ws - inner;
            }
            it = ws;
        }
        else
        {
            attr.quote = 0;
            attr.valueSpan.offset = nameEnd;
            attr.valueSpan.length = 0;
            attr.valueWithQuotes.offset = nameEnd;
            attr.valueWithQuotes.length = 0;
            it = nameEnd;
        }

        attr.whole.offset = nameStart;
        attr.whole.length = it - nameStart;
        attr.isStyle = IEquals(attr.name, "style");
        if (attr.isStyle)
            ParseStyleDeclarations(attr.valueSpan.offset, attr.valueSpan.length, attr.styleDecls);

        attrs.push_back(attr);
        return it;
    };

    while (pos < n)
    {
        int lt = static_cast<int>(text_.find('<', pos));
        if (lt < 0)
        {
            addLeaf(RmlNodeKind::Text, pos, n, text_.substr(pos, n - pos));
            break;
        }
        if (lt > pos)
            addLeaf(RmlNodeKind::Text, pos, lt, text_.substr(pos, lt - pos));

        // Comment.
        if (lt + 4 <= n && text_.compare(lt, 4, "<!--") == 0)
        {
            int close = static_cast<int>(text_.find("-->", lt + 4));
            const int end = (close < 0) ? n : close + 3;
            addLeaf(RmlNodeKind::Comment, lt, end, text_.substr(lt, end - lt));
            pos = end;
            continue;
        }
        // Raw declaration: <?...?> or <!...> (doctype / CDATA / processing instruction).
        if (lt + 1 < n && (text_[lt + 1] == '?' || text_[lt + 1] == '!'))
        {
            int close = static_cast<int>(text_.find('>', lt + 2));
            const int end = (close < 0) ? n : close + 1;
            addLeaf(RmlNodeKind::Raw, lt, end, text_.substr(lt, end - lt));
            pos = end;
            continue;
        }
        // Closing tag.
        if (lt + 1 < n && text_[lt + 1] == '/')
        {
            int i = lt + 2;
            while (i < n && !IsSpace(text_[i]) && text_[i] != '>')
                ++i;
            const std::string name = Lower(text_.substr(lt + 2, i - (lt + 2)));
            int gt = static_cast<int>(text_.find('>', i));
            const int end = (gt < 0) ? n : gt + 1;

            for (int si = static_cast<int>(stack.size()) - 1; si >= 1; --si)
            {
                if (Lower(nodes_[stack[si]].tag) == name)
                {
                    RmlNode& el = nodes_[stack[si]];
                    el.innerSpan.length = lt - el.innerSpan.offset;
                    el.whole.length = end - el.whole.offset;
                    stack.resize(si);
                    break;
                }
            }
            pos = end;
            continue;
        }
        // Opening tag.
        if (lt + 1 < n && IsNameStart(text_[lt + 1]))
        {
            int i = lt + 1;
            const int nameStart = i;
            while (i < n && IsNameChar(text_[i]))
                ++i;
            const std::string name = text_.substr(nameStart, i - nameStart);

            std::vector<RmlAttribute> attrs;
            bool selfClosing = false;
            int tagEnd = n;
            while (i < n)
            {
                while (i < n && IsSpace(text_[i]))
                    ++i;
                if (i >= n)
                    break;
                if (text_[i] == '/' && i + 1 < n && text_[i + 1] == '>')
                {
                    selfClosing = true;
                    tagEnd = i + 2;
                    break;
                }
                if (text_[i] == '>')
                {
                    tagEnd = i + 1;
                    break;
                }
                const int before = i;
                i = parseAttribute(i, attrs);
                if (i <= before)
                    ++i; // guarantee progress
            }

            RmlNode el;
            el.kind = RmlNodeKind::Element;
            el.parent = stack.back();
            el.tag = name;
            el.selfClosing = selfClosing;
            el.attributes = std::move(attrs);
            el.whole.offset = lt;
            el.whole.length = tagEnd - lt;
            el.openTag.offset = lt;
            el.openTag.length = tagEnd - lt;
            el.nameSpan.offset = nameStart;
            el.nameSpan.length = static_cast<int>(name.size());
            el.innerSpan.offset = tagEnd;
            el.innerSpan.length = 0;

            const int idx = static_cast<int>(nodes_.size());
            nodes_[stack.back()].children.push_back(idx);
            nodes_.push_back(el);

            const std::string lower = Lower(name);
            const bool rawText = (lower == "style" || lower == "script");

            if (!selfClosing && rawText)
            {
                // Swallow raw content up to the matching close tag; never parse markup inside it.
                stack.push_back(idx);
                const std::string needle = Lower("</" + name);
                const int found = FindLower(text_, needle, tagEnd);
                const int contentEnd = (found < 0) ? n : found;
                addLeaf(RmlNodeKind::Raw, tagEnd, contentEnd, text_.substr(tagEnd, contentEnd - tagEnd));
                stack.pop_back();
                nodes_[idx].innerSpan.length = contentEnd - tagEnd;
                int gt = (found < 0) ? n : static_cast<int>(text_.find('>', found));
                const int closeEnd = (gt < 0) ? n : gt + 1;
                nodes_[idx].whole.length = closeEnd - nodes_[idx].whole.offset;
                pos = closeEnd;
                continue;
            }

            if (!selfClosing)
                stack.push_back(idx);
            pos = tagEnd;
            continue;
        }
        // A '<' that is not markup: keep it as text.
        addLeaf(RmlNodeKind::Text, lt, lt + 1, "<");
        pos = lt + 1;
    }

    // Implicitly close anything left open at EOF.
    while (stack.size() > 1)
    {
        RmlNode& el = nodes_[stack.back()];
        el.innerSpan.length = n - el.innerSpan.offset;
        el.whole.length = n - el.whole.offset;
        stack.pop_back();
    }
}

void RmlTextModel::ParseStyleDeclarations(int valueOffset, int valueLength, std::vector<RmlStyleDecl>& out) const
{
    const int end = valueOffset + valueLength;
    int i = valueOffset;
    while (i <= end)
    {
        // Find next ';' outside parens/quotes.
        int semi = i;
        int depth = 0;
        char q = 0;
        for (; semi < end; ++semi)
        {
            const char c = text_[semi];
            if (q)
            {
                if (c == q)
                    q = 0;
            }
            else if (c == '"' || c == '\'')
                q = c;
            else if (c == '(')
                ++depth;
            else if (c == ')')
                --depth;
            else if (c == ';' && depth <= 0)
                break;
        }

        int ds = i, de = semi;
        while (ds < de && IsSpace(text_[ds]))
            ++ds;
        while (de > ds && IsSpace(text_[de - 1]))
            --de;
        if (ds < de)
        {
            int colon = -1;
            int d2 = 0;
            char q2 = 0;
            for (int k = ds; k < de; ++k)
            {
                const char c = text_[k];
                if (q2)
                {
                    if (c == q2)
                        q2 = 0;
                }
                else if (c == '"' || c == '\'')
                    q2 = c;
                else if (c == '(')
                    ++d2;
                else if (c == ')')
                    --d2;
                else if (c == ':' && d2 <= 0)
                {
                    colon = k;
                    break;
                }
            }
            if (colon >= 0)
            {
                RmlStyleDecl decl;
                decl.property = text_.substr(ds, colon - ds);
                // Trim property.
                {
                    int a = ds, b = colon;
                    while (a < b && IsSpace(text_[a]))
                        ++a;
                    while (b > a && IsSpace(text_[b - 1]))
                        --b;
                    decl.property = text_.substr(a, b - a);
                }

                int vs = colon + 1, ve = de;
                // Detect and exclude a trailing "!important" from the value span.
                {
                    std::string seg = text_.substr(vs, ve - vs);
                    const std::string imp = "!important";
                    const size_t ip = seg.rfind(imp);
                    if (ip != std::string::npos)
                    {
                        decl.important = true;
                        ve = vs + static_cast<int>(ip);
                    }
                }
                int a = vs, b = ve;
                while (a < b && IsSpace(text_[a]))
                    ++a;
                while (b > a && IsSpace(text_[b - 1]))
                    --b;
                decl.value = text_.substr(a, b - a);
                decl.valueSpan.offset = a;
                decl.valueSpan.length = b - a;
                decl.whole.offset = ds;
                decl.whole.length = de - ds;
                out.push_back(decl);
            }
        }
        if (semi >= end)
            break;
        i = semi + 1;
    }
}

void RmlTextModel::DetectStyle()
{
    detectedQuote_ = '"';
    for (const RmlNode& node : nodes_)
    {
        for (const RmlAttribute& a : node.attributes)
        {
            if (a.quote == '\'' && a.valueWithQuotes.length > 0)
            {
                detectedQuote_ = '\'';
                break;
            }
        }
    }

    detectedIndent_ = "  ";
    for (size_t idx = 1; idx < nodes_.size(); ++idx)
    {
        const RmlNode& node = nodes_[idx];
        if (node.kind != RmlNodeKind::Element || node.parent <= 0)
            continue;
        const int lineStart = (node.whole.offset > 0) ? static_cast<int>(text_.rfind('\n', node.whole.offset) + 1) : 0;
        std::string lead;
        for (int k = lineStart; k < node.whole.offset && IsSpace(text_[k]) && text_[k] != '\n'; ++k)
            lead.push_back(text_[k]);
        if (!lead.empty())
        {
            detectedIndent_ = lead;
            break;
        }
    }
}

int RmlTextModel::CountElements() const
{
    int count = 0;
    for (const RmlNode& node : nodes_)
        if (node.kind == RmlNodeKind::Element)
            ++count;
    return count;
}

int RmlTextModel::FindFirstElement(const std::string& tag) const
{
    const std::string want = Lower(tag);
    int result = -1;
    std::function<void(int)> dfs = [&](int i)
    {
        if (result >= 0)
            return;
        if (i != 0 && nodes_[i].kind == RmlNodeKind::Element && Lower(nodes_[i].tag) == want)
        {
            result = i;
            return;
        }
        for (int c : nodes_[i].children)
            dfs(c);
    };
    dfs(0);
    return result;
}

int RmlTextModel::ResolvePath(const std::vector<int>& path) const
{
    int cur = 0;
    for (int idx : path)
    {
        if (cur < 0 || cur >= static_cast<int>(nodes_.size()))
            return -1;
        const std::vector<int>& kids = nodes_[cur].children;
        if (idx < 0 || idx >= static_cast<int>(kids.size()))
            return -1;
        cur = kids[idx];
    }
    return cur;
}

int RmlTextModel::ChildIndexOf(int node) const
{
    if (node <= 0)
        return -1;
    const int p = nodes_[node].parent;
    if (p < 0)
        return -1;
    const std::vector<int>& kids = nodes_[p].children;
    for (size_t i = 0; i < kids.size(); ++i)
        if (kids[i] == node)
            return static_cast<int>(i);
    return -1;
}

int RmlTextModel::ElementChildCount(int parent) const
{
    if (parent < 0 || parent >= static_cast<int>(nodes_.size()))
        return 0;
    int c = 0;
    for (int k : nodes_[parent].children)
        if (nodes_[k].kind == RmlNodeKind::Element)
            ++c;
    return c;
}

int RmlTextModel::ElementChildAt(int parent, int ordinal) const
{
    if (parent < 0 || parent >= static_cast<int>(nodes_.size()))
        return -1;
    int seen = 0;
    for (int k : nodes_[parent].children)
    {
        if (nodes_[k].kind == RmlNodeKind::Element)
        {
            if (seen == ordinal)
                return k;
            ++seen;
        }
    }
    return -1;
}

const RmlAttribute* RmlTextModel::FindAttribute(int node, const std::string& name) const
{
    if (node < 0 || node >= static_cast<int>(nodes_.size()))
        return nullptr;
    for (const RmlAttribute& a : nodes_[node].attributes)
        if (a.name == name)
            return &a;
    return nullptr;
}

void RmlTextModel::ApplyPatch(const RmlSpan& span, const std::string& replacement)
{
    text_.replace(span.offset, span.length, replacement);
    std::string snapshot = text_;
    Load(snapshot);
}

bool RmlTextModel::SetAttribute(int node, const std::string& name, const std::string& value)
{
    RmlPatch p;
    if (!ComputeAttributePatch(node, name, value, p))
        return false;
    ApplyPatch(p.span, p.replacement);
    return true;
}

bool RmlTextModel::RemoveAttribute(int node, const std::string& name)
{
    RmlPatch p;
    if (!ComputeAttributeRemovalPatch(node, name, p))
        return false;
    ApplyPatch(p.span, p.replacement);
    return true;
}

bool RmlTextModel::GetStyleProperty(int node, const std::string& property, std::string& outValue) const
{
    const RmlAttribute* style = FindAttribute(node, "style");
    if (!style)
        return false;
    for (const RmlStyleDecl& d : style->styleDecls)
    {
        if (IEquals(d.property, property))
        {
            outValue = d.value;
            return true;
        }
    }
    return false;
}

bool RmlTextModel::SetStyleProperty(int node, const std::string& property, const std::string& value)
{
    RmlPatch p;
    if (!ComputeStylePropertyPatch(node, property, value, p))
        return false;
    ApplyPatch(p.span, p.replacement);
    return true;
}

bool RmlTextModel::RemoveStyleProperty(int node, const std::string& property)
{
    RmlPatch p;
    if (!ComputeStyleRemovePatch(node, property, p))
        return false;
    ApplyPatch(p.span, p.replacement);
    return true;
}

int RmlTextModel::InsertElement(int parent, int childOrdinal, const std::string& markup)
{
    RmlPatch p;
    if (!ComputeInsertPatch(parent, childOrdinal, markup, p))
        return -1;
    ApplyPatch(p.span, p.replacement);
    return FindFirstElement(markupTagOf(markup));
}

bool RmlTextModel::RemoveElement(int node)
{
    RmlPatch p;
    if (!ComputeElementRemovalPatch(node, p))
        return false;
    ApplyPatch(p.span, p.replacement);
    return true;
}

// ---------------------------------------------------------------------------
// Patch computation (const, against the current parse) + batch apply.
// ---------------------------------------------------------------------------

bool RmlTextModel::ComputeAttributePatch(int node, const std::string& name, const std::string& value, RmlPatch& out) const
{
    if (node < 0 || node >= static_cast<int>(nodes_.size()) || nodes_[node].kind != RmlNodeKind::Element)
        return false;
    const RmlNode& el = nodes_[node];
    for (const RmlAttribute& a : el.attributes)
    {
        if (a.name == name)
        {
            out.span = a.valueSpan;
            out.replacement = value;
            return true;
        }
    }
    const std::string q(1, detectedQuote_);
    out.span.offset = el.nameSpan.End();
    out.span.length = 0;
    out.replacement = " " + name + "=" + q + value + q;
    return true;
}

bool RmlTextModel::ComputeAttributeRemovalPatch(int node, const std::string& name, RmlPatch& out) const
{
    if (node < 0 || node >= static_cast<int>(nodes_.size()))
        return false;
    for (const RmlAttribute& a : nodes_[node].attributes)
    {
        if (a.name == name)
        {
            RmlSpan s = a.whole;
            int o = s.offset;
            if (o > 0 && text_[o - 1] == ' ')
            {
                --o;
                s.offset = o;
                s.length += 1;
            }
            out.span = s;
            out.replacement = "";
            return true;
        }
    }
    return false;
}

bool RmlTextModel::ComputeStylePropertyPatch(int node, const std::string& property, const std::string& value, RmlPatch& out) const
{
    if (node < 0 || node >= static_cast<int>(nodes_.size()) || nodes_[node].kind != RmlNodeKind::Element)
        return false;

    const RmlAttribute* style = FindAttribute(node, "style");
    if (!style)
    {
        // Create the style attribute carrying a single declaration.
        const RmlNode& el = nodes_[node];
        const std::string q(1, detectedQuote_);
        out.span.offset = el.nameSpan.End();
        out.span.length = 0;
        out.replacement = " style=" + q + property + ": " + value + q;
        return true;
    }

    for (const RmlStyleDecl& d : style->styleDecls)
    {
        if (IEquals(d.property, property))
        {
            out.span = d.valueSpan;
            out.replacement = value;
            return true;
        }
    }

    out.span.offset = style->valueSpan.End();
    out.span.length = 0;
    out.replacement = style->styleDecls.empty() ? (property + ": " + value) : ("; " + property + ": " + value);
    return true;
}

bool RmlTextModel::ComputeStyleRemovePatch(int node, const std::string& property, RmlPatch& out) const
{
    const RmlAttribute* style = FindAttribute(node, "style");
    if (!style)
        return false;
    const int n = static_cast<int>(text_.size());
    for (const RmlStyleDecl& d : style->styleDecls)
    {
        if (IEquals(d.property, property))
        {
            RmlSpan s = d.whole;
            int e = s.End();
            while (e < n && text_[e] == ' ')
                ++e;
            if (e < n && text_[e] == ';')
            {
                ++e;
                if (e < n && text_[e] == ' ')
                    ++e;
                s.length = e - s.offset;
            }
            else
            {
                int o = s.offset;
                while (o > 0 && text_[o - 1] == ' ')
                    --o;
                if (o > 0 && text_[o - 1] == ';')
                {
                    --o;
                    s.offset = o;
                }
                s.length = d.whole.End() - s.offset;
            }
            out.span = s;
            out.replacement = "";
            return true;
        }
    }
    return false;
}

bool RmlTextModel::ComputeTextPatch(int node, const std::string& newText, RmlPatch& out) const
{
    if (node < 0 || node >= static_cast<int>(nodes_.size()))
        return false;
    const RmlNode& nd = nodes_[node];
    if (nd.kind != RmlNodeKind::Text && nd.kind != RmlNodeKind::Raw)
        return false;
    out.span = nd.span;
    out.replacement = newText;
    return true;
}

bool RmlTextModel::ComputeElementRemovalPatch(int node, RmlPatch& out) const
{
    if (node <= 0 || node >= static_cast<int>(nodes_.size()) || nodes_[node].kind != RmlNodeKind::Element)
        return false;
    out.span = nodes_[node].whole;
    out.replacement = "";
    return true;
}

bool RmlTextModel::ComputeInsertPatch(int parent, int childOrdinal, const std::string& markup, RmlPatch& out) const
{
    if (parent < 0 || parent >= static_cast<int>(nodes_.size()))
        return false;
    const RmlNode& p = nodes_[parent];

    if (p.selfClosing)
    {
        // Expand <tag .../>  ->  <tag ...>markup</tag>
        out.span.offset = p.openTag.End() - 2; // the "/>"
        out.span.length = 2;
        out.replacement = std::string(">") + markup + "</" + p.tag + ">";
        return true;
    }

    int insertOffset = -1;
    const int elemCount = ElementChildCount(parent);
    if (childOrdinal >= elemCount)
        insertOffset = p.innerSpan.End();
    else
    {
        const int before = ElementChildAt(parent, childOrdinal);
        if (before < 0)
            return false;
        insertOffset = nodes_[before].whole.offset;
    }

    // Put the new node on its own line, matching a sibling's indentation when present.
    std::string prefix = "\n";
    if (!p.children.empty())
    {
        const RmlNode& firstChild = nodes_[p.children.front()];
        const int anchor = (firstChild.kind == RmlNodeKind::Element) ? firstChild.whole.offset : p.innerSpan.offset;
        const int lineStart = (anchor > 0) ? static_cast<int>(text_.rfind('\n', anchor) + 1) : 0;
        std::string lead;
        for (int k = lineStart; k < anchor && IsSpace(text_[k]) && text_[k] != '\n'; ++k)
            lead.push_back(text_[k]);
        prefix = "\n" + lead;
    }

    out.span.offset = insertOffset;
    out.span.length = 0;
    out.replacement = markup + prefix;
    return true;
}

void RmlTextModel::ApplyPatches(const std::vector<RmlPatch>& patches)
{
    std::vector<RmlPatch> ps = patches;
    // Descending offset so earlier splices never shift the offsets of not-yet-applied (lower) ones.
    std::sort(ps.begin(), ps.end(), [](const RmlPatch& a, const RmlPatch& b) { return a.span.offset > b.span.offset; });
    for (const RmlPatch& p : ps)
        text_.replace(p.span.offset, p.span.length, p.replacement);
    std::string snapshot = text_;
    Load(snapshot);
}

std::string RmlTextModel::markupTagOf(const std::string& markup) const
{
    const size_t lt = markup.find('<');
    if (lt == std::string::npos)
        return std::string();
    size_t i = lt + 1;
    while (i < markup.size() && IsNameChar(markup[i]))
        ++i;
    return markup.substr(lt + 1, i - (lt + 1));
}

std::string RmlTextModel::MakeElement(const std::string& tag, const std::string& id, const std::string& className,
    const std::string& style, bool selfClose, int styleSampleNode) const
{
    (void)styleSampleNode;
    const std::string q(1, detectedQuote_);
    std::string s = "<" + tag;
    if (!id.empty())
        s += " id=" + q + id + q;
    if (!className.empty())
        s += " class=" + q + className + q;
    if (!style.empty())
        s += " style=" + q + style + q;
    if (selfClose)
        s += " />";
    else
        s += "></" + tag + ">";
    return s;
}
