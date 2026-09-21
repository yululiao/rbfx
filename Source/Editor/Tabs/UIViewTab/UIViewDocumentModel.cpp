//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewDocumentModel.h"

#include <Urho3D/Core/Format.h>
#include <Urho3D/IO/Log.h>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <cctype>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>

namespace Urho3D
{

namespace
{
ea::string ToStr(const Rml::String& s)
{
    return ea::string(s.c_str(), s.length());
}

Rml::String FromStr(const ea::string& s)
{
    return Rml::String(s.c_str(), s.length());
}

void TrimRml(Rml::String& s)
{
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == Rml::String::npos)
        s.clear();
    else
        s = s.substr(b, s.find_last_not_of(ws) - b + 1);
}

ea::string Ea(const std::string& s)
{
    return ea::string(s.c_str(), s.length());
}

std::string Std(const ea::string& s)
{
    return std::string(s.c_str(), s.length());
}

std::string LowerStd(const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

/// True when a source text run carries visible content (letters/digits/punctuation).
/// Whitespace-only runs between tags are *not* modeled: they stay in the spine and are
/// never patched, so indentation and newlines round-trip byte-for-byte.
bool IsMeaningfulText(const std::string& t)
{
    for (char c : t)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
            return true;
    }
    return false;
}

std::string RawAttrValue(const RmlTextModel& src, int node, const char* name)
{
    const RmlAttribute* a = src.FindAttribute(node, name);
    return a ? a->value : std::string();
}

/// Build a model node anchored to source element \\a nodeIdx. Values are taken verbatim
/// from the source text (bindings, data-* tokens and non-string attributes included), so
/// an unedited document round-trips byte-for-byte. \\a srcNode_ is the spine anchor used by
/// the save-time reconcile.
SharedPtr<UiNode> BuildElement(const RmlTextModel& src, int nodeIdx)
{
    const RmlNode& sn = src.Node(nodeIdx);
    auto node = MakeShared<UiNode>();
    node->srcNode_ = nodeIdx;
    node->tag_ = Ea(sn.tag);
    node->id_ = Trim(Ea(RawAttrValue(src, nodeIdx, "id")));
    node->classes_ = Trim(Ea(RawAttrValue(src, nodeIdx, "class")));

    for (const RmlAttribute& a : sn.attributes)
    {
        const std::string lower = LowerStd(a.name);
        if (lower == "id" || lower == "class" || lower == "style")
            continue;
        node->attributes_.emplace_back(Ea(a.name), Ea(a.value));
    }
    ea::sort(node->attributes_.begin(), node->attributes_.end(),
        [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
        { return a.first < b.first; });

    const RmlAttribute* style = src.FindAttribute(nodeIdx, "style");
    if (style != nullptr)
    {
        for (const RmlStyleDecl& d : style->styleDecls)
            node->style_.push_back(UiStyleDecl{Ea(d.property), Ea(d.value)});
    }

    for (int cidx : sn.children)
    {
        const RmlNode& cn = src.Node(cidx);
        if (cn.kind == RmlNodeKind::Element)
        {
            node->children_.push_back(BuildElement(src, cidx));
        }
        else if (cn.kind == RmlNodeKind::Text && IsMeaningfulText(cn.text))
        {
            auto text = MakeShared<UiNode>();
            text->tag_ = "#text";
            text->text_ = Ea(cn.text);
            text->srcNode_ = cidx;
            node->children_.push_back(text);
        }
        // Comments, raw blocks and whitespace text are intentionally not modeled; they live
        // only in the spine and are preserved untouched across save.
    }
    return node;
}

/// True when \a cand can stand for \a child tag- and id-wise. Template-minted
/// structure carries its own ids, so a DOM element with a different id must
/// never absorb an id-less model node's link: the window frame would steal
/// edits meant for the authored content nested inside it.
bool TagIdCompatible(const UiNode& child, Rml::Element* cand)
{
    return ToStr(cand->GetTagName()) == child.tag_ && ToStr(cand->GetId()) == child.id_;
}

/// How many of \a cand's attribute names the model node also declares. Values
/// are not compared: data bindings ({{...}}) come out substituted in the DOM,
/// while the names survive, so overlap still tells same-tag candidates apart
/// inside template-generated structure (e.g. data-model).
int AttributeOverlap(const UiNode& child, Rml::Element* cand)
{
    int score = 0;
    for (const auto& pair : cand->GetAttributes())
    {
        const ea::string name = ToStr(pair.first);
        if (name == "id" || name == "class" || name == "style")
            continue;
        for (const auto& attr : child.attributes_)
        {
            if (attr.first == name)
            {
                ++score;
                break;
            }
        }
    }
    return score;
}

/// DOM elements already claimed by a model node. Their subtrees are skipped
/// during rescue searches so nothing gets double-booked.
using UsedDomElems = ea::unordered_set<const Rml::Element*>;

struct SubtreeMatch
{
    Rml::Element* element = nullptr;
    int score = 0;
};

/// Best unused counterpart of \a child anywhere inside \a parent's subtree,
/// in document order, preferring the candidate with the highest attribute-name
/// overlap (ties stay in document order). Data bindings ({{...}}) come out
/// substituted in the DOM while the attribute names survive, so overlap still
/// tells same-tag candidates apart inside template-generated structure.
SubtreeMatch FindFreeMatch(Rml::Element* parent, const UiNode& child, const UsedDomElems& used)
{
    SubtreeMatch best;
    const int count = parent->GetNumChildren(false);
    for (int i = 0; i < count; ++i)
    {
        Rml::Element* cand = parent->GetChild(i);
        if (!cand || cand->GetTagName() == "#text" || used.find(cand) != used.end())
            continue;
        if (TagIdCompatible(child, cand))
        {
            const SubtreeMatch here{cand, AttributeOverlap(child, cand)};
            if (!best.element || here.score > best.score)
                best = here;
        }
        const SubtreeMatch deeper = FindFreeMatch(cand, child, used);
        if (deeper.element && (!best.element || deeper.score > best.score))
            best = deeper;
    }
    return best;
}

/// Best-effort attach of the live preview DOM to each element node's dom_ by
/// structural correlation. Plain documents line up as direct children;
/// templated bodies (e.g. <body template="window">) relocate the authored
/// nodes into the template's content slot, so children without a direct match
/// are rescued by a subtree search. Text children are not correlated: the
/// whole projection is rebuilt from the emitted text on every edit, so a text
/// node never needs a live element to push changes onto. Whatever still cannot
/// be correlated stays null; the model remains the source of truth regardless.
void CorrelateDomRec(UiNode& node, Rml::Element* element, UsedDomElems& used)
{
    node.dom_ = element;
    if (!element)
        return;
    used.insert(element);

    // Pass 1: direct children in document order, exactly how a plain document
    // lays them out. Already-claimed candidates (grabbed by an earlier rescue)
    // are skipped so nothing is double-booked.
    const int numChildren = element->GetNumChildren(false);
    int cursor = 0;
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child->IsText())
            continue; // no live-element link for text nodes (see note above)
        Rml::Element* match = nullptr;
        int matchIndex = -1;
        for (int i = cursor; i < numChildren; ++i)
        {
            Rml::Element* cand = element->GetChild(i);
            if (!cand || used.find(cand) != used.end())
                continue;
            if (TagIdCompatible(*child, cand))
            {
                match = cand;
                matchIndex = i;
                break;
            }
        }
        if (match)
        {
            cursor = matchIndex + 1;
            child->dom_ = match;
            used.insert(match);
        }
    }

    // Pass 2: rescue children that found no direct counterpart (the templated
    // case). Each rescue is marked used immediately so later siblings cannot
    // double-book it and results stay deterministic.
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child->IsText() || child->dom_)
            continue;
        const SubtreeMatch found = FindFreeMatch(element, *child, used);
        if (found.element)
        {
            child->dom_ = found.element;
            used.insert(found.element);
        }
    }

    for (const SharedPtr<UiNode>& child : node.children_)
        CorrelateDomRec(*child, child->dom_, used);
}

void CorrelateDom(UiNode& node, Rml::Element* element)
{
    UsedDomElems used;
    CorrelateDomRec(node, element, used);
}

/// Generate standard RML for a wholly-new subtree (every node lacks a spine anchor). Used at
/// save to insert editor-created widgets; no proprietary syntax is emitted.
std::string GenSubtree(const UiNode& node, int depth)
{
    if (node.IsText())
        return Std(EscapeRmlText(node.text_));

    const std::string q = "\"";
    std::string s = "<" + Std(node.tag_);
    if (!node.id_.empty())
        s += " id=" + q + Std(EscapeRmlText(node.id_)) + q;
    if (!node.classes_.empty())
        s += " class=" + q + Std(EscapeRmlText(node.classes_)) + q;
    for (const auto& attr : node.attributes_)
        s += " " + Std(attr.first) + "=" + q + Std(EscapeRmlText(attr.second)) + q;
    const std::string style = Std(FormatStyleDeclarations(node.style_));
    if (!style.empty())
        s += " style=" + q + style + q;

    if (node.children_.empty())
        return s + "/>";

    s += ">";
    const std::string pad(static_cast<size_t>(depth + 1) * 2, ' ');
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child->IsText())
            s += GenSubtree(*child, depth + 1);
        else
            s += "\n" + pad + GenSubtree(*child, depth + 1);
    }
    const std::string closePad(static_cast<size_t>(depth) * 2, ' ');
    if (!node.children_.front()->IsText())
        s += "\n" + closePad;
    s += "</" + Std(node.tag_) + ">";
    return s;
}

/// Diff one anchored node's own attributes/style/id/class against the spine and queue the
/// minimal set of value patches. Untouched values emit nothing (byte-for-byte preserved).
void ReconcileSelf(const UiNode& node, int srcIdx, const RmlTextModel& src, std::vector<RmlPatch>& out)
{
    auto setAttr = [&](const char* name, const ea::string& want)
    {
        const RmlAttribute* a = src.FindAttribute(srcIdx, name);
        const std::string w = Std(want);
        if (a && want.empty())
        {
            RmlPatch p;
            if (src.ComputeAttributeRemovalPatch(srcIdx, name, p))
                out.push_back(p);
        }
        else if (!a && !want.empty())
        {
            RmlPatch p;
            if (src.ComputeAttributePatch(srcIdx, name, w, p))
                out.push_back(p);
        }
        else if (a && !want.empty() && a->value != w)
        {
            RmlPatch p;
            if (src.ComputeAttributePatch(srcIdx, name, w, p))
                out.push_back(p);
        }
    };

    setAttr("id", node.id_);
    setAttr("class", node.classes_);

    // Generic attributes (id/class/style already excluded from node.attributes_).
    for (const auto& attr : node.attributes_)
        setAttr(attr.first.c_str(), attr.second);
    const RmlNode& sn = src.Node(srcIdx);
    for (const RmlAttribute& a : sn.attributes)
    {
        const std::string lower = LowerStd(a.name);
        if (lower == "id" || lower == "class" || lower == "style")
            continue;
        bool present = false;
        for (const auto& attr : node.attributes_)
        {
            if (attr.first == Ea(a.name))
            {
                present = true;
                break;
            }
        }
        if (!present)
        {
            RmlPatch p;
            if (src.ComputeAttributeRemovalPatch(srcIdx, a.name, p))
                out.push_back(p);
        }
    }

    // Inline style: patch changed/new declarations, remove dropped ones. The spine keeps
    // authored order; new properties append, which is still valid CSS.
    const RmlAttribute* style = src.FindAttribute(srcIdx, "style");
    for (const UiStyleDecl& decl : node.style_)
    {
        const std::string name = Std(decl.name_);
        const std::string want = Std(decl.value_);
        const RmlStyleDecl* cur = nullptr;
        if (style)
        {
            for (const RmlStyleDecl& d : style->styleDecls)
            {
                if (LowerStd(d.property) == LowerStd(name))
                {
                    cur = &d;
                    break;
                }
            }
        }
        if (!cur || cur->value != want)
        {
            RmlPatch p;
            if (src.ComputeStylePropertyPatch(srcIdx, name, want, p))
                out.push_back(p);
        }
    }
    if (style)
    {
        for (const RmlStyleDecl& d : style->styleDecls)
        {
            bool kept = false;
            for (const UiStyleDecl& decl : node.style_)
            {
                if (LowerStd(Std(decl.name_)) == LowerStd(d.property))
                {
                    kept = true;
                    break;
                }
            }
            if (!kept)
            {
                RmlPatch p;
                if (src.ComputeStyleRemovePatch(srcIdx, d.property, p))
                    out.push_back(p);
            }
        }
    }
}

/// Recursively collect save-time patches: self diffs for anchored nodes, whole-subtree
/// generation + insertion for editor-created nodes, and removal patches for anchored source
/// children that no longer have a model node.
void ReconcileNode(const UiNode& node, const RmlTextModel& src, std::vector<RmlPatch>& out)
{
    if (node.IsText())
    {
        if (node.srcNode_ >= 0 && Ea(src.Node(node.srcNode_).text) != node.text_)
        {
            RmlPatch p;
            if (src.ComputeTextPatch(node.srcNode_, Std(node.text_), p))
                out.push_back(p);
        }
        return;
    }

    if (node.srcNode_ < 0)
        return; // whole subtree handled by the parent's insert; nothing anchored to patch

    const int srcIdx = node.srcNode_;
    ReconcileSelf(node, srcIdx, src, out);

    // Which source children are still referenced by an anchored model child.
    ea::vector<int> anchoredSrc;
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child->srcNode_ >= 0)
            anchoredSrc.push_back(child->srcNode_);
    }
    auto referenced = [&](int sidx)
    {
        for (int a : anchoredSrc)
        {
            if (a == sidx)
                return true;
        }
        return false;
    };

    // Deletions: an anchored source element/meaningful-text child that no model child maps
    // to was removed in the editor -> patch its bytes out. Whitespace text (never modeled)
    // and comments/raw are untouched, so original layout survives.
    const RmlNode& sn = src.Node(srcIdx);
    for (int cidx : sn.children)
    {
        const RmlNode& cn = src.Node(cidx);
        if (referenced(cidx))
            continue;
        RmlPatch p;
        if (cn.kind == RmlNodeKind::Element && src.ComputeElementRemovalPatch(cidx, p))
            out.push_back(p);
        else if (cn.kind == RmlNodeKind::Text && IsMeaningfulText(cn.text) && src.ComputeTextPatch(cidx, "", p))
            out.push_back(p);
    }

    // Recurse into anchored children; run-serialize consecutive new children into one append.
    size_t i = 0;
    const size_t count = node.children_.size();
    while (i < count)
    {
        const UiNode& child = *node.children_[i];
        if (child.srcNode_ >= 0)
        {
            ReconcileNode(child, src, out);
            ++i;
            continue;
        }
        if (child.IsText())
        {
            ++i; // editor-added bare text under an anchored parent is not spliced here
            continue;
        }
        std::string markup;
        while (i < count)
        {
            const UiNode& runNode = *node.children_[i];
            if (runNode.srcNode_ >= 0 || runNode.IsText())
                break;
            if (!markup.empty())
                markup += "\n";
            markup += GenSubtree(runNode, 0);
            ++i;
        }
        RmlPatch p;
        if (src.ComputeInsertPatch(srcIdx, src.ElementChildCount(srcIdx), markup, p))
            out.push_back(p);
    }
}

bool CollectPath(const UiNode* node, const UiNode* child, ea::vector<unsigned>& path)
{
    for (unsigned i = 0; i < node->children_.size(); i++)
    {
        const UiNode* candidate = node->children_[i];
        path.push_back(i);
        if (candidate == child || CollectPath(candidate, child, path))
            return true;
        path.pop_back();
    }
    return false;
}

UiNode* FindByDomRecursive(const UiNode* node, const Rml::Element* element)
{
    if (node->dom_ == element)
        return const_cast<UiNode*>(node);
    for (const SharedPtr<UiNode>& child : node->children_)
    {
        if (UiNode* found = FindByDomRecursive(child, element))
            return found;
    }
    return nullptr;
}
UiNode* FindParentRecursive(const UiNode* node, const UiNode* child)
{
    for (const SharedPtr<UiNode>& candidate : node->children_)
    {
        if (candidate == child)
            return const_cast<UiNode*>(node);
        if (UiNode* found = FindParentRecursive(candidate, child))
            return found;
    }
    return nullptr;
}

} // namespace

int UiNode::FindStyle(const ea::string& name) const
{
    for (int i = 0; i < (int)style_.size(); i++)
    {
        if (style_[i].name_ == name)
            return i;
    }
    return -1;
}

ea::string UiNode::GetStyle(const ea::string& name) const
{
    const int index = FindStyle(name);
    return index >= 0 ? style_[index].value_ : ea::string();
}

void UiNode::SetStyle(const ea::string& name, const ea::string& value)
{
    const int index = FindStyle(name);
    if (index >= 0)
        style_[index].value_ = value;
    else
        style_.push_back(UiStyleDecl{name, value});
}

void UiNode::RemoveStyle(const ea::string& name)
{
    const int index = FindStyle(name);
    if (index >= 0)
        style_.erase(style_.begin() + index);
}

bool UiNode::IsMaterialized() const
{
    if (GetStyle("position") != "absolute")
        return false;
    float tmp = 0.0f;
    return TryParsePx(GetStyle("left"), tmp) && TryParsePx(GetStyle("top"), tmp)
        && TryParsePx(GetStyle("width"), tmp) && TryParsePx(GetStyle("height"), tmp);
}

UiNodePayload SnapshotUiNodePayload(const UiNode& node)
{
    UiNodePayload payload;
    payload.text_ = node.text_;
    payload.id_ = node.id_;
    payload.classes_ = node.classes_;
    payload.attributes_ = node.attributes_;
    payload.style_ = node.style_;
    return payload;
}

void ApplyUiNodePayload(UiNode& node, const UiNodePayload& payload)
{
    node.text_ = payload.text_;
    node.id_ = payload.id_;
    node.classes_ = payload.classes_;
    node.attributes_ = payload.attributes_;
    node.style_ = payload.style_;
}

SharedPtr<UiNode> DeepCloneUiNode(const UiNode& src)
{
    auto copy = MakeShared<UiNode>();
    copy->tag_ = src.tag_;
    copy->text_ = src.text_;
    copy->id_ = src.id_;
    copy->classes_ = src.classes_;
    copy->attributes_ = src.attributes_;
    copy->style_ = src.style_;
    for (const SharedPtr<UiNode>& child : src.children_)
        copy->children_.push_back(DeepCloneUiNode(*child));
    return copy;
}

ea::string Trim(const ea::string& s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == ea::string::npos)
        return ea::string();
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

void ParseStyleDeclarations(const ea::string& text, ea::vector<UiStyleDecl>& out)
{
    out.clear();
    size_t begin = 0;
    while (begin < text.length())
    {
        size_t end = text.find(';', begin);
        const bool last = end == ea::string::npos;
        const ea::string decl = Trim(text.substr(begin, (last ? text.length() : end) - begin));
        begin = last ? text.length() : end + 1;
        if (decl.empty())
            continue;
        const size_t colon = decl.find(':');
        if (colon == ea::string::npos)
            continue;
        const ea::string name = Trim(decl.substr(0, colon));
        const ea::string value = Trim(decl.substr(colon + 1));
        if (name.empty() || value.empty())
            continue;
        out.push_back(UiStyleDecl{name, value});
    }
}

ea::string FormatStyleDeclarations(const ea::vector<UiStyleDecl>& decls)
{
    ea::string style;
    for (const UiStyleDecl& decl : decls)
    {
        if (!style.empty())
            style += "; ";
        style += decl.name_ + ": " + decl.value_;
    }
    return style;
}

bool UiDocumentModel::BuildFromText(const ea::string& sourceText, Rml::ElementDocument* document)
{
    root_.Reset();
    // The spine keeps the ORIGINAL text (pre token-substitution), so {{bindings}} and
    // data-model="{{__data_model_id}}" are the model's truth and are written back verbatim.
    if (!source_.Load(Std(sourceText)))
        return false;
    const int body = source_.FindFirstElement("body");
    if (body < 0)
        return false;

    // The ElementDocument corresponds to <body>: build the tree from the source spine and
    // attach the live preview DOM to dom_ only by structural correlation (best effort).
    root_ = BuildElement(source_, body);
    CorrelateDom(*root_, document);
    return true;
}

/// CSS numbers must never use scientific notation (%g can emit '1e+06').
/// Uses plain snprintf: rbfx's Format() is fmt-based ({} syntax) and would
/// leave printf-style specifiers like "%g" in the output verbatim.
ea::string FormatCssNumber(double value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", value);
    if (strchr(buf, 'e') || strchr(buf, 'E'))
        snprintf(buf, sizeof(buf), "%.2f", value);
    return ea::string(buf);
}

ea::string UiDocumentModel::EmitRml() const
{
    // Reconcile against the spine, which still reflects the load-time parse (commands never
    // mutate source_), so every srcNode_ anchor is valid here. All patch offsets are computed
    // against that one parse, then applied in a single descending batch -> untouched bytes
    // (head, styles, templates, comments, {{bindings}}, whitespace) stay byte-for-byte intact.
    std::vector<RmlPatch> patches;
    if (root_)
        ReconcileNode(*root_, source_, patches);

    RmlTextModel spine = source_;
    spine.ApplyPatches(patches);
    return Ea(spine.GetText());
}

UiNode* UiDocumentModel::FindByDom(const Rml::Element* element) const
{
    if (root_ && element)
        return FindByDomRecursive(root_, element);
    return nullptr;
}

UiNode* UiDocumentModel::FindParent(const UiNode* node) const
{
    if (!root_ || !node || node == root_)
        return nullptr;
    return FindParentRecursive(root_, node);
}

bool UiDocumentModel::BuildPath(const UiNode* node, ea::vector<unsigned>& path) const
{
    path.clear();
    if (!root_ || !node || node == root_)
        return node != nullptr;
    return CollectPath(root_, node, path);
}

UiNode* UiDocumentModel::ResolvePath(const ea::vector<unsigned>& path) const
{
    UiNode* node = root_;
    for (unsigned index : path)
    {
        if (!node || index >= node->children_.size())
            return nullptr;
        node = node->children_[index];
    }
    return node;
}

UiTransform ParseUiTransform(const ea::string& value)
{
    UiTransform result;
    const Rml::String text = FromStr(value);
    size_t begin = 0;
    while (begin < text.size())
    {
        const size_t open = text.find('(', begin);
        if (open == Rml::String::npos)
            break;
        const size_t close = text.find(')', open);
        if (close == Rml::String::npos)
            break;
        Rml::String name = text.substr(begin, open - begin);
        TrimRml(name);
        const Rml::String args = text.substr(open + 1, close - open - 1);
        begin = close + 1;

        if (name == "rotate")
        {
            ea::string angle = Trim(ToStr(args));
            angle.replace("deg", "");
            angle = Trim(angle);
            result.rotateDeg_ = strtof(angle.c_str(), nullptr);
        }
        else if (name == "scale")
        {
            Rml::StringList parts;
            Rml::StringUtilities::ExpandString(parts, args, ',');
            if (parts.size() == 1)
            {
                const float s = strtof(parts[0].c_str(), nullptr);
                result.scaleX_ = s;
                result.scaleY_ = s;
            }
            else if (parts.size() >= 2)
            {
                result.scaleX_ = strtof(parts[0].c_str(), nullptr);
                result.scaleY_ = strtof(parts[1].c_str(), nullptr);
            }
        }
    }
    return result;
}

ea::string FormatUiTransform(const UiTransform& t)
{
    const bool rotated = fabsf(t.rotateDeg_) > 1e-4f;
    const bool scaled = fabsf(t.scaleX_ - 1.0f) > 1e-4f || fabsf(t.scaleY_ - 1.0f) > 1e-4f;
    if (!rotated && !scaled)
        return ea::string();

    ea::string out;
    if (rotated)
        out += "rotate(" + FormatCssNumber((double)t.rotateDeg_) + "deg)";
    if (scaled)
    {
        if (!out.empty())
            out += " ";
        out += fabsf(t.scaleX_ - t.scaleY_) < 1e-4f
            ? "scale(" + FormatCssNumber((double)t.scaleX_) + ")"
            : "scale(" + FormatCssNumber((double)t.scaleX_) + ", " + FormatCssNumber((double)t.scaleY_) + ")";
    }
    return out;
}

bool TryParsePx(const ea::string& value, float& out)
{
    const ea::string text = Trim(value);
    if (text.empty())
        return false;
    char* end = nullptr;
    const float parsed = strtof(text.c_str(), &end);
    if (end == text.c_str())
        return false;
    ea::string suffix = Trim(end ? ea::string(end) : ea::string());
    for (auto& c : suffix)
        c = (char)tolower((unsigned char)c);
    if (!suffix.empty() && suffix != "px")
        return false; // %, em, dp, calc(...) etc. are not explicit px values
    out = parsed;
    return true;
}

ea::string FormatPx(float value)
{
    return FormatCssNumber((double)value) + "px";
}

ea::string EscapeRmlText(const ea::string& s)
{
    ea::string out;
    out.reserve(s.length());
    for (char c : s)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

}
