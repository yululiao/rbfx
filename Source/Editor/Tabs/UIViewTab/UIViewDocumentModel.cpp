//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

// DOM half of the editor document model: attaching the live preview to the
// built tree (dom_ correlation), nested-template chrome collection and the
// RmlUi string helpers. The text half (spine seeding, tree construction,
// save-time reconcile, head commands and formatting helpers) lives in
// UIViewDocumentModelText.cpp, free of RmlUi so UiRmlCI can exercise the
// build<->emit pipeline headless.

#include "UIViewDocumentModel.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Variant.h>

#include <stdlib.h>

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
        if (child->IsNestedDoc())
            continue; // view-only virtual node; gets the body element below
        if (child->IsHeadLink())
            continue; // head construct; no preview DOM to correlate
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
        if (child->IsText() || child->IsNestedDoc() || child->IsHeadLink() || child->dom_)
            continue;
        const SubtreeMatch found = FindFreeMatch(element, *child, used);
        if (found.element)
        {
            child->dom_ = found.element;
            used.insert(found.element);
        }
    }

    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child->IsNestedDoc() || child->IsHeadLink())
            continue;
        CorrelateDomRec(*child, child->dom_, used);
    }
}

void CorrelateDom(UiNode& node, Rml::Element* element)
{
    UsedDomElems used;
    CorrelateDomRec(node, element, used);
}

/// Every live DOM element some model node claimed during correlation,
/// collected from the dom_ links (the same set CorrelateDom built). The
/// root's own element (the document) lands in the set too; no direct
/// child's subtree contains it, so it cannot mark a subtree as authored.
void CollectClaimedDom(const UiNode& node, UsedDomElems& out)
{
    if (node.IsNestedDoc() || node.IsHeadLink())
        return; // view-only virtual nodes stand for no authored content
    if (node.dom_)
        out.insert(node.dom_);
    for (const SharedPtr<UiNode>& child : node.children_)
        CollectClaimedDom(*child, out);
}

/// True when any element of \a element's subtree was claimed by the model.
bool SubtreeHasClaimedDom(Rml::Element* element, const UsedDomElems& claimed)
{
    if (claimed.find(element) != claimed.end())
        return true;
    const int count = element->GetNumChildren(false);
    for (int i = 0; i < count; ++i)
    {
        Rml::Element* child = element->GetChild(i);
        if (child && SubtreeHasClaimedDom(child, claimed))
            return true;
    }
    return false;
}

/// Direct children of the live document whose subtree hosts no authored
/// content: the chrome the nested template minted into this document
/// (window frame, title bar, resize handles). The template's content
/// container drops out naturally - correlation places the authored elements
/// inside it, so its subtree is claimed.
ea::vector<Rml::Element*> CollectNestedChrome(const UiNode& root, Rml::ElementDocument* document)
{
    UsedDomElems claimed;
    CollectClaimedDom(root, claimed);

    ea::vector<Rml::Element*> chrome;
    const int count = document->GetNumChildren(false);
    for (int i = 0; i < count; ++i)
    {
        Rml::Element* child = document->GetChild(i);
        if (!child || child->GetTagName() == "#text")
            continue;
        if (!SubtreeHasClaimedDom(child, claimed))
            chrome.push_back(child);
    }
    return chrome;
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

} // namespace

bool UiDocumentModel::BuildFromText(const ea::string& sourceText, Rml::ElementDocument* document)
{
    // Two stages: the DOM-free tree build first (headless-testable, see
    // BuildTreeFromText), then the live-preview attachment on top of it.
    if (!BuildTreeFromText(sourceText))
        return false;
    AttachDocument(document);
    return true;
}

void UiDocumentModel::AttachDocument(Rml::ElementDocument* document)
{
    if (!root_)
        return;
    // A null document is legal: correlation degenerates to leaving every
    // dom_ null, and the model stays fully usable for text-only work.
    CorrelateDom(*root_, document);

    // The virtual node projects onto the body element (so hit testing and
    // the overlay gates treat it as live), while its outline covers only
    // the chrome the nested template minted into the document - not the
    // whole document canvas the body's own box would give.
    if (UiNode* nested = GetNestedDoc())
    {
        nested->dom_ = root_->dom_;
        if (document)
            nested->nestedChromeElems_ = CollectNestedChrome(*root_, document);
    }
}

UiNode* UiDocumentModel::FindByDom(const Rml::Element* element) const
{
    if (root_ && element)
        return FindByDomRecursive(root_, element);
    return nullptr;
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

}
