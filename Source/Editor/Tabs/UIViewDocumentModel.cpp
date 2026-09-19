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
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EASTL/sort.h>

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

/// Recursive DOM walk building model nodes. Text nodes collapse to a single
/// "#text" node; everything unrecognized stays in attributes/inline style
/// untouched so that it round-trips through EmitRml() unharmed.
SharedPtr<UiNode> BuildNode(Rml::Element* element)
{
    auto node = MakeShared<UiNode>();
    node->dom_ = element;
    node->tag_ = ToStr(element->GetTagName());

    if (auto* text = rmlui_dynamic_cast<Rml::ElementText*>(element))
    {
        node->tag_ = "#text";
        node->text_ = ToStr(text->GetText());
        return node;
    }

    node->id_ = Trim(ToStr(element->GetId()));
    node->classes_ = Trim(ToStr(element->GetClassNames()));

    // Snapshot the raw attributes so unknown ones (src, href, data-*, ...)
    // survive the round-trip. The parser keeps id/class/style in the
    // attribute map as well, so strip them from the generic list.
    for (const auto& pair : element->GetAttributes())
    {
        const ea::string name = ToStr(pair.first);
        if (name == "id" || name == "class" || name == "style")
            continue;
        ea::string value;
        Rml::String raw = pair.second.Get<Rml::String>(); // empty for non-string variants
        value = ToStr(raw);
        node->attributes_.emplace_back(name, value);
    }
    ea::sort(node->attributes_.begin(), node->attributes_.end(),
        [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
        { return a.first < b.first; });

    // Inline style: parse the authored "style" attribute text so that
    // declaration order is preserved instead of relying on resolved order.
    const Rml::Variant* style = element->GetAttribute("style");
    if (style != nullptr)
        ParseStyleDeclarations(ToStr(style->Get<Rml::String>()), node->style_);

    const int numChildren = element->GetNumChildren(true);
    for (int i = 0; i < numChildren; i++)
    {
        Rml::Element* child = element->GetChild(i);
        // The body element is folded into the document node of the model.
        if (child->GetTagName() == "body")
            continue;
        node->children_.push_back(BuildNode(child));
    }
    return node;
}

void EmitNode(const UiNode& node, int indent, Rml::String& out)
{
    const Rml::String pad(static_cast<size_t>(indent) * 2, ' ');

    if (node.IsText())
    {
        out += EscapeRmlText(node.text_);
        return;
    }

    out += pad + "<" + FromStr(node.tag_);
    if (!node.id_.empty())
        out += " id=\"" + EscapeRmlText(node.id_) + "\"";
    if (!node.classes_.empty())
        out += " class=\"" + EscapeRmlText(node.classes_) + "\"";
    for (const auto& attr : node.attributes_)
        out += " " + FromStr(attr.first) + "=\"" + EscapeRmlText(attr.second) + "\"";

    Rml::String style;
    for (const auto& decl : node.style_)
        style += FromStr(decl.name_) + ": " + FromStr(decl.value_) + "; ";
    TrimRml(style);
    if (!style.empty())
        out += " style=\"" + style + "\"";

    if (node.children_.empty())
    {
        out += "/>\n";
        return;
    }

    out += ">";

    // Mixed content: children are emitted inline (no injected whitespace) so
    // that text layout is preserved byte-for-byte through the round-trip.
    const bool mixed = std::any_of(node.children_.begin(), node.children_.end(),
        [](const SharedPtr<UiNode>& child) { return child->IsText(); });

    if (mixed)
    {
        for (const auto& child : node.children_)
        {
            if (child->IsText())
                out += EscapeRmlText(child->text_);
            else
                EmitNode(*child, indent, out); // own line inside mixed content
        }
        out += "</" + FromStr(node.tag_) + ">\n";
        return;
    }

    out += "\n";
    for (const auto& child : node.children_)
        EmitNode(*child, indent + 1, out);
    out += pad + "</" + FromStr(node.tag_) + ">\n";
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

void UiDocumentModel::BuildFromDom(Rml::ElementDocument* document)
{
    root_.Reset();
    if (document == nullptr)
        return;

    // The ElementDocument is created from the <body> element, so it carries
    // the body's id/inline style directly; its children are the content.
    root_ = BuildNode(document);
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
    Rml::String out = "<rml>\n";
    if (!headRaw_.empty())
    {
        out += FromStr(headRaw_);
        if (out.back() != '\n')
            out += "\n";
    }
    if (root_)
        EmitNode(*root_, 0, out);
    out += "</rml>\n";
    return ToStr(out);
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
    for (const SharedPtr<UiNode>& child : root_->children_)
    {
        if (child == node)
            return root_;
        if (UiNode* found = FindParent(child))
            return found;
    }
    return nullptr;
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
