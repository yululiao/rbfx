//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

// DOM-free half of the editor document model: spine seeding, tree construction
// from text, the save-time reconcile and every text-level command. Everything
// here is pure text/tree logic (std + EASTL only), which is what lets UiRmlCI
// compile and regression-test the build<->emit round-trip headless, without
// RmlUi or the engine. The DOM correlation half stays in UIViewDocumentModel.cpp.

#include "UIViewDocumentModel.h"
#include "UIViewParagraphText.h"

#include <algorithm>
#include <cctype>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EASTL/sort.h>

namespace Urho3D
{

namespace
{

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

/// Build a model node anchored to source element \a nodeIdx. Values are taken verbatim
/// from the source text (bindings, data-* tokens and non-string attributes included), so
/// an unedited document round-trips byte-for-byte. \a srcNode_ is the spine anchor used by
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
        if (!a)
        {
            if (!want.empty())
            {
                RmlPatch p;
                if (src.ComputeAttributePatch(srcIdx, name, w, p))
                    out.push_back(p);
            }
            return;
        }
        // Model "" equals BOTH a valueless attribute (disabled, checked, ...) and an
        // authored foo="" - an unedited document must round-trip each verbatim. Only
        // a value the editor actually cleared (spine bytes were non-empty) removes
        // the attribute; real deletions go through the absent-from-model scan below.
        if (want.empty() && !a->value.empty())
        {
            RmlPatch p;
            if (src.ComputeAttributeRemovalPatch(srcIdx, name, p))
                out.push_back(p);
        }
        else if (!want.empty() && a->value != w)
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

    // Inline style. The spine keeps authored order. Any MULTI-change style edit is
    // coalesced into a single patch by construction; per-declaration patches for such
    // batches collide (two creations insert whole style attributes at the same offset
    // and mint `style="a" style="b"`; two appends into an empty style="" merge
    // without a ';' separator; adjacent removals share one ';'). Only value changes
    // plus at most ONE structural add or remove stay on the surgical byte-preserving path.
    const RmlAttribute* style = src.FindAttribute(srcIdx, "style");

    // Self-heal documents saved by older builds: duplicate style attributes are
    // invalid RML, born from that same-offset double insert. Any whole rewrite keeps
    // the FIRST attribute (rewritten to carry the model's declarations) and drops the rest.
    int styleAttrCount = 0;
    for (const RmlAttribute& a : sn.attributes)
    {
        if (LowerStd(a.name) == "style")
            ++styleAttrCount;
    }

    size_t adds = 0, removals = 0;
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
                ++removals;
        }
        for (const UiStyleDecl& decl : node.style_)
        {
            bool present = false;
            for (const RmlStyleDecl& d : style->styleDecls)
            {
                if (LowerStd(d.property) == LowerStd(Std(decl.name_)))
                {
                    present = true;
                    break;
                }
            }
            if (!present)
                ++adds;
        }
    }

    // Untouched style: the model's declarations still equal the FIRST style
    // attribute verbatim (BuildElement copied them from it, in order). Even a
    // damaged document (duplicate style attributes written by an older build)
    // must round-trip byte-for-byte while unedited: the extras only collapse
    // once the style is actually edited.
    bool styleEdited = !style;
    if (style)
    {
        const auto& spineDecls = style->styleDecls;
        if (spineDecls.size() != node.style_.size())
            styleEdited = true;
        else
            for (size_t k = 0; k < spineDecls.size(); ++k)
            {
                if (LowerStd(spineDecls[k].property) != LowerStd(Std(node.style_[k].name_))
                    || spineDecls[k].value != Std(node.style_[k].value_))
                {
                    styleEdited = true;
                    break;
                }
            }
    }

    if (!style && !node.style_.empty())
    {
        // Create the whole style attribute in ONE patch, never one per declaration.
        RmlPatch p;
        if (src.ComputeStyleDeclarationPatch(srcIdx, Std(FormatStyleDeclarations(node.style_)), p))
            out.push_back(p);
    }
    else if (style && node.style_.empty())
    {
        // No inline style left: drop every style attribute outright instead of
        // leaving a style="" husk (which re-seeded the no-separator merge bug
        // on the next multi-add). Each removal claims its own leading space.
        for (const RmlAttribute& a : sn.attributes)
        {
            if (LowerStd(a.name) != "style")
                continue;
            RmlPatch p;
            if (src.ComputeAttributeRemovalPatch(a, p))
                out.push_back(p);
        }
    }
    else if ((styleAttrCount > 1 && styleEdited) || adds + removals >= 2)
    {
        // One whole-value rewrite covers every add/change/remove at once, so the
        // batch can never mint duplicate attributes or separator-less merges. An
        // unedited damaged document (duplicate style) deliberately does NOT land
        // here: it falls through to the surgical path, which emits nothing, so
        // even broken input round-trips byte-for-byte until it is edited.
        RmlPatch p;
        if (src.ComputeStyleDeclarationPatch(srcIdx, Std(FormatStyleDeclarations(node.style_)), p))
            out.push_back(p);
        bool firstSeen = false;
        for (const RmlAttribute& a : sn.attributes)
        {
            if (LowerStd(a.name) != "style")
                continue;
            if (!firstSeen)
            {
                firstSeen = true;
                continue;
            }
            RmlPatch extra;
            if (src.ComputeAttributeRemovalPatch(a, extra))
                out.push_back(extra);
        }
    }
    else
    {
        // Surgical: any number of value changes plus at most one add or one remove.
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
}

/// Emission for a node whose text-run children a command rebuilt (a paragraph
/// Content edit). The generic children pass below cannot express this: it only
/// appends editor-created children at the inner end and skips bare text. The
/// whole child region is therefore reconciled as one batch instead - removed
/// anchored children patched out, changed runs patched in place, and the
/// editor-created runs spliced at their position with raw text / generated
/// <br/> markup. The batch math lives in the pure, headless-testable
/// UIViewParagraphText unit; this adapter only converts model nodes to it.
void ReconcileTextRunsChildren(const UiNode& node, const RmlTextModel& src, std::vector<RmlPatch>& out)
{
    std::vector<ParagraphChildState> states;
    states.reserve(node.children_.size());
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        ParagraphChildState state;
        state.srcNode = child->srcNode_;
        state.isText = child->IsText();
        if (state.isText)
            state.text = Std(child->text_);
        if (state.srcNode < 0)
            state.markup = state.isText ? Std(child->text_) : GenSubtree(*child, 0);
        states.push_back(std::move(state));
    }
    ComputeParagraphChildrenPatches(src, node.srcNode_, states, out);
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

    if (node.textRunsRebuilt_)
    {
        // A paragraph whose text/br children a command rebuilt: reconcile the
        // run region as a unit (patches + positional splices).
        ReconcileTextRunsChildren(node, src, out);
        return;
    }

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
        if (child.IsNestedDoc() || child.IsHeadLink())
        {
            ++i; // view-only virtual nodes; never serialized into the source
            continue;
        }
        std::string markup;
        while (i < count)
        {
            const UiNode& runNode = *node.children_[i];
            if (runNode.srcNode_ >= 0 || runNode.IsText() || runNode.IsNestedDoc()
                || runNode.IsHeadLink())
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

/// One <link> element of <head> as (spine node index, trimmed type, trimmed
/// href), in document order. \a headIdx receives the <head> node index, or
/// -1 when the document has none.
struct HeadLinkEntry
{
    int nodeIdx_ = -1;
    ea::string type_;
    ea::string href_;
};

ea::vector<HeadLinkEntry> CollectHeadLinks(const RmlTextModel& spine, int& headIdx)
{
    headIdx = spine.FindFirstElement("head");
    ea::vector<HeadLinkEntry> out;
    if (headIdx < 0)
        return out;
    for (int linkIdx : spine.Node(headIdx).children)
    {
        const RmlNode& link = spine.Node(linkIdx);
        if (link.kind != RmlNodeKind::Element || LowerStd(link.tag) != "link")
            continue;
        HeadLinkEntry entry;
        entry.nodeIdx_ = linkIdx;
        const std::string type = RawAttrValue(spine, linkIdx, "type");
        const std::string href = RawAttrValue(spine, linkIdx, "href");
        const size_t tb = type.find_first_not_of(" \t\r\n");
        entry.type_ = Ea(tb == std::string::npos ? std::string()
            : type.substr(tb, type.find_last_not_of(" \t\r\n") - tb + 1));
        const size_t hb = href.find_first_not_of(" \t\r\n");
        entry.href_ = Ea(hb == std::string::npos ? std::string()
            : href.substr(hb, href.find_last_not_of(" \t\r\n") - hb + 1));
        out.push_back(entry);
    }
    return out;
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

ea::string UiNode::GetAttribute(const ea::string& name) const
{
    for (const auto& attribute : attributes_)
    {
        if (attribute.first == name)
            return attribute.second;
    }
    return ea::string();
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

bool UiDocumentModel::BuildTreeFromText(const ea::string& sourceText)
{
    root_.Reset();
    // The spine keeps the ORIGINAL text (pre token-substitution), so {{bindings}} and
    // data-model="{{__data_model_id}}" are the model's truth and are written back verbatim.
    if (!source_.Load(Std(sourceText)))
        return false;
    const int body = source_.FindFirstElement("body");
    if (body < 0)
        return false;

    // The ElementDocument corresponds to <body>: build the tree from the source spine.
    // (The live preview DOM is attached separately by AttachDocument.)
    root_ = BuildElement(source_, body);

    // Every <link> of <head> becomes exactly ONE virtual child ahead of the
    // body content, in authored order. A plain link is a #head-link node -
    // except the template link <body template="..."> instantiates: that one
    // is represented by the #nested-doc node, which carries its chrome
    // projection, navigation and per-link editing together (a link must never
    // appear as two nodes). With several template links the first one is the
    // instantiated one; the rest stay #head-link. Like all virtual nodes they
    // are view-only: never serialized, never DOM-correlated; edits go through
    // the text-level head commands.
    int headIdx = -1;
    const ea::vector<HeadLinkEntry> headLinks = CollectHeadLinks(source_, headIdx);
    const std::string templateName = RawAttrValue(source_, body, "template");
    const bool hasInstantiatedTemplate = !Trim(Ea(templateName)).empty();
    bool templateTaken = false;
    ea::vector<SharedPtr<UiNode>> headNodes;
    for (const HeadLinkEntry& entry : headLinks)
    {
        auto node = MakeShared<UiNode>();
        if (hasInstantiatedTemplate && entry.type_ == "text/template" && !templateTaken)
        {
            templateTaken = true;
            node->tag_ = "#nested-doc";
            node->nestedDocHref_ = entry.href_;
        }
        else
            node->tag_ = "#head-link";
        node->headLinkOrdinal_ = static_cast<unsigned>(headNodes.size());
        node->attributes_.emplace_back("type", entry.type_);
        node->attributes_.emplace_back("href", entry.href_);
        headNodes.push_back(node);
    }
    root_->children_.insert(root_->children_.begin(), headNodes.begin(), headNodes.end());
    return true;
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

namespace
{
/// Normalize a parsed fragment node into "editor-made" content: strip the
/// anchors BuildElement seeded from the fragment-local spine (the save-time
/// reconcile must fully generate these nodes, like AddWidget products, not
/// patch spans of a spine they never belonged to) and trim text runs - the
/// parser's text runs swallow the generated indentation between siblings,
/// which a re-serialize would double.
void NormalizeFragmentNode(UiNode& node)
{
    node.srcNode_ = -1;
    if (node.IsText())
        node.text_ = Trim(node.text_);
    for (const SharedPtr<UiNode>& child : node.children_)
        NormalizeFragmentNode(*child);
}
}

ea::string UiDocumentModel::SerializeSubtree(const UiNode& node) const
{
    return Ea(GenSubtree(node, 0));
}

ea::vector<SharedPtr<UiNode>> UiDocumentModel::ParseFragment(const ea::string& rmlText) const
{
    // A fragment has no <body>: its root-level ELEMENTS are the pasteables (loose
    // text is dropped - copy never produces it, so accepting it would only let
    // pasted prose into the document). The parse is deliberately lenient
    // (RmlTextModel::Load never fails), so the element gate is what rejects
    // plain prose / garbage input. Comments and whitespace runs never paste.
    ea::vector<SharedPtr<UiNode>> out;
    RmlTextModel spine;
    spine.Load(Std(rmlText));
    for (int cidx : spine.Node(spine.Root()).children)
    {
        const RmlNode& cn = spine.Node(cidx);
        if (cn.kind != RmlNodeKind::Element)
            continue;
        SharedPtr<UiNode> node = BuildElement(spine, cidx);
        NormalizeFragmentNode(*node);
        out.push_back(node);
    }
    return out;
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

ea::vector<ea::string> UiDocumentModel::GetTemplateLinks() const
{
    // Walk the spine's <head> (the editor tree starts at <body>, but the spine
    // indexes the whole document). <head> is nested under <rml>, so locate it
    // by pre-order search - the same way BuildTreeFromText finds <body> - rather
    // than as a direct child of the synthetic root.
    ea::vector<ea::string> out;
    const int head = source_.FindFirstElement("head");
    if (head < 0)
        return out;
    for (int linkIdx : source_.Node(head).children)
    {
        const RmlNode& link = source_.Node(linkIdx);
        if (link.kind != RmlNodeKind::Element || LowerStd(link.tag) != "link")
            continue;
        const std::string type = LowerStd(RawAttrValue(source_, linkIdx, "type"));
        std::string href = RawAttrValue(source_, linkIdx, "href");
        const size_t hb = href.find_first_not_of(" \t\r\n");
        href = hb == std::string::npos ? std::string()
            : href.substr(hb, href.find_last_not_of(" \t\r\n") - hb + 1);
        if (type == "text/template" && !href.empty())
            out.push_back(Ea(href));
    }
    return out;
}

bool UiDocumentModel::InsertHeadLink(const ea::string& text, const ea::string& type,
    const ea::string& href, ea::string& out)
{
    out = text;
    const ea::string trimmedType = Trim(type);
    const ea::string trimmedHref = Trim(href);
    if (trimmedType.empty() || trimmedHref.empty())
        return false;

    // The live spine (source_) must never be re-parsed or mutated from here:
    // every tree node's srcNode_ anchor indexes its node list, and anchors are
    // only rebuilt by a full ReloadFromText. So edit a throwaway parse of the
    // emitted text; the reload makes the edit canonical for the real spine.
    RmlTextModel spine;
    if (!spine.Load(Std(text)))
        return false;

    int headIdx = -1;
    const auto existing = CollectHeadLinks(spine, headIdx);
    if (headIdx < 0)
        return false; // no <head> to host the link; growing one is out of scope
    const std::string loweredType = LowerStd(Std(trimmedType));
    for (const HeadLinkEntry& entry : existing)
    {
        if (LowerStd(Std(entry.type_)) == loweredType && entry.href_ == trimmedHref)
            return false; // already linked: keep the head duplicate-free
    }

    const std::string markup = "<link type=\"" + Std(trimmedType) + "\" href=\""
        + Std(EscapeRmlText(trimmedHref)) + "\"/>";
    // Right after the last existing link, so the authored order of the other
    // links stays put (rcss cascade order matters: a later sheet overrides an
    // earlier one at equal specificity). In a link-less head the new link
    // becomes the first child, so an inline <style> still loads after it and
    // can override the sheet.
    int childOrdinal = 0;
    if (!existing.empty())
    {
        int ordinal = 0;
        for (int child : spine.Node(headIdx).children)
        {
            if (child == existing.back().nodeIdx_)
                break;
            if (spine.Node(child).kind == RmlNodeKind::Element)
                ++ordinal;
        }
        childOrdinal = ordinal + 1;
    }
    if (spine.InsertElement(headIdx, childOrdinal, markup) < 0)
        return false;
    out = Ea(spine.GetText());
    return true;
}

bool UiDocumentModel::EditHeadLinkAt(const ea::string& text, unsigned ordinal,
    const ea::string& type, const ea::string& href, ea::string& out)
{
    out = text;
    const ea::string trimmedType = Trim(type);
    const ea::string trimmedHref = Trim(href);
    if (trimmedType.empty() || trimmedHref.empty())
        return false;

    RmlTextModel spine;
    if (!spine.Load(Std(text)))
        return false;

    int headIdx = -1;
    const auto links = CollectHeadLinks(spine, headIdx);
    if (ordinal >= links.size())
        return false; // stale ordinal: the document was rebuilt with fewer links
    const int target = links[ordinal].nodeIdx_;

    // Both attribute patches are computed against the one stable parse and
    // applied in a single descending batch. A missing attribute is added in
    // place (ComputeAttributePatch inserts right after the tag name).
    RmlPatch typePatch;
    if (!spine.ComputeAttributePatch(target, "type", Std(trimmedType), typePatch))
        return false;
    RmlPatch hrefPatch;
    if (!spine.ComputeAttributePatch(target, "href", Std(EscapeRmlText(trimmedHref)), hrefPatch))
        return false;
    spine.ApplyPatches({typePatch, hrefPatch});
    out = Ea(spine.GetText());
    return true;
}

bool UiDocumentModel::RemoveHeadLinkAt(const ea::string& text, unsigned ordinal, ea::string& out)
{
    out = text;
    RmlTextModel spine;
    if (!spine.Load(Std(text)))
        return false;

    int headIdx = -1;
    const auto links = CollectHeadLinks(spine, headIdx);
    if (ordinal >= links.size())
        return false; // stale ordinal: the document was rebuilt with fewer links

    // Remove the whole authored line - leading indent and trailing newline
    // included - not just the element bytes: a bare element removal would
    // leave an empty line behind in the head.
    const std::string& buf = spine.GetText();
    const RmlNode& link = spine.Node(links[ordinal].nodeIdx_);
    int begin = link.whole.offset;
    while (begin > 0 && (buf[begin - 1] == ' ' || buf[begin - 1] == '\t'))
        --begin;
    int end = link.whole.End();
    while (end < static_cast<int>(buf.size())
        && (buf[end] == ' ' || buf[end] == '\t'))
        ++end;
    if (end < static_cast<int>(buf.size()) && buf[end] == '\r')
        ++end;
    if (end < static_cast<int>(buf.size()) && buf[end] == '\n')
        ++end;

    spine.ApplyPatches({RmlPatch{RmlSpan{begin, end - begin}, std::string()}});
    out = Ea(spine.GetText());
    return true;
}

UiNode* UiDocumentModel::GetNestedDoc() const
{
    if (root_ && !root_->children_.empty())
    {
        UiNode* first = root_->children_.front().Get();
        if (first->IsNestedDoc())
            return first;
    }
    return nullptr;
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
