//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#include "UIViewInspector.h"
#include "UIViewHelpers.h"
#include "UIViewParagraphText.h"

#include "UIViewTab.h"

#include "../../Project/Project.h"

#include <Urho3D/SystemUI/SystemUI.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StyleTypes.h>

#include <EASTL/sort.h>

namespace Urho3D
{

EditorTab* UIViewInspector::GetOwnerTab()
{
    return owner_;
}

// ---------------------------------------------------------------------------
// UIViewInspector: edits the selected model node
// ---------------------------------------------------------------------------

UIViewInspector::UIViewInspector(UIViewTab* owner)
    : Object(owner->GetContext())
    , owner_(owner)
{
}

void UIViewInspector::RenderContent()
{
    UIViewTab* tab = owner_;

    // Document-level editing applies with or without a selection, so it sits
    // above the per-node editors.
    RenderHeadLinks();
    ui::Separator();

    UiNode* node = tab ? tab->GetSelectedNode() : nullptr;
    if (!node)
    {
        ui::TextDisabled("Select an element to edit its attributes.");
        return;
    }

    // The nested-doc virtual node has no authored source in this document:
    // show the navigation panel instead of the attribute/style editors.
    if (node->IsNestedDoc())
    {
        RenderNestedDoc(node);
        return;
    }

    // A #head-link node edits its <head> bytes through the text-level link
    // commands; the generic attribute/style editors below do not apply.
    if (node->IsHeadLink())
    {
        RenderHeadLink(node);
        return;
    }

    ea::string header = node->tag_;
    if (!node->id_.empty())
        header += "#" + node->id_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (doc && node == doc->GetModel().root_.Get())
    {
        // The document root is a real element (inline style on <body> is worth
        // editing), but it must never read as just another widget panel: edits
        // meant for a selected widget have landed on <body> unnoticed.
        ui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
            ICON_FA_TRIANGLE_EXCLAMATION " Document Root (%s)", header.c_str());
    }
    else
        ui::Text(ICON_FA_HAND_POINTER " %s", header.c_str());
    // The file this element's source lives in (matters with several documents
    // open and for template-minted areas, whose source lives elsewhere).
    if (doc)
        ui::TextDisabled(ICON_FA_FILE " %s", doc->GetSourcePath().c_str());
    ui::Separator();

    // A committed edit rebuilds the model tree and dangles `node`; stop
    // rendering for this frame instead of touching freed memory below.
    if (RenderVisibility(node))
        return;
    if (RenderTextContent(node))
        return;
    if (RenderAttributes(node))
        return;
    ui::Separator();
    if (RenderLayout(node))
        return;
    if (RenderAppearance(node))
        return;
    if (RenderInlineStyle(node))
        return;
    ui::Separator();
    RenderTemplates(node);
    ui::Separator();
    RenderComputed(node);
}

void UIViewInspector::RenderHeadLinks()
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;

    ui::SeparatorText(ICON_FA_LINK " Head Links");
    if (!doc || !doc->GetRmlDocument())
    {
        ui::TextDisabled("(no document open)");
        return;
    }

    // The links themselves are #head-link nodes at the top of the Hierarchy
    // (edit each there); this row is only the spigot for one more <link>.
    ui::PushItemWidth(-40.0f);
    const bool committed = ui::InputTextWithHint("##headLinkHref", "/UI/default.rcss",
        headLinkHrefBuf_, sizeof(headLinkHrefBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    // Pick an existing file instead of typing its path; either kind can be
    // chosen, so the filter lists both and the + buttons below record which.
    if (auto picked = ResourceBrowseWidget("##hlAddBrowse", context_, "", "rcss,rml"))
        snprintf(headLinkHrefBuf_, sizeof(headLinkHrefBuf_), "%s", picked->c_str());
    const bool wantCss = committed || ui::Button(ICON_FA_PLUS " Stylesheet (.rcss)");
    ui::SameLine();
    const bool wantRml = ui::Button(ICON_FA_PLUS " Template (.rml)");
    if (wantCss || wantRml)
    {
        // Enter in the field adds the common case: a stylesheet link.
        const ea::string href = Trim(ea::string(headLinkHrefBuf_));
        if (!href.empty() && doc->AddHeadLink(wantCss ? "text/rcss" : "text/template", href))
            headLinkHrefBuf_[0] = '\0';
    }
}

void UIViewInspector::InvalidateCaches()
{
    // Whole-tree rebuilds (every command / undo) replace all nodes; the cached
    // inline-style and paragraph-content texts must be reseeded from the new
    // node on the next render.
    styleSeedValid_ = false;
    contentSeedValid_ = false;
}

void UIViewInspector::RenderNestedDoc(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc)
        return;

    const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), node->nestedDocHref_);
    const bool exists = !resPath.empty() && ResourceExists(context_, resPath);

    ui::Text(ICON_FA_CUBES " Nested Document");
    ui::TextDisabled(ICON_FA_FILE " %s", resPath.c_str());
    ui::Separator();

    ui::TextWrapped(
        "The window frame of this document (title bar, close button, ...) is\n"
        "minted from this template file. Open it to edit the frame itself.");
    ui::Spacing();

    // The <link type="text/template"> href this node stands for: editing it
    // repoints the window frame at another template file.
    char hrefBuf[512];
    snprintf(hrefBuf, sizeof(hrefBuf), "%s", node->nestedDocHref_.c_str());
    ui::PushItemWidth(-40.0f);
    const bool hrefEdited = ui::InputText("href", hrefBuf, sizeof(hrefBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    // This link always names a template, so the picker lists .rml only.
    const auto pickedHref = ResourceBrowseWidget("##nestedBrowse", context_, node->nestedDocHref_, "rml");
    if (hrefEdited || pickedHref)
    {
        const ea::string newHref = hrefEdited ? Trim(ea::string(hrefBuf)) : *pickedHref;
        if (!newHref.empty())
        {
            doc->EditHeadLink(node, "text/template", newHref);
            return; // the model was rebuilt; re-render from the new node
        }
    }

    ui::BeginDisabled(!exists);
    if (ui::Button(ICON_FA_MAGNIFYING_GLASS " Reveal in Resource Browser"))
        OpenNestedDocumentFile(context_, doc, node->nestedDocHref_, /*revealOnly=*/true);
    ui::SameLine();
    if (ui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open"))
        OpenNestedDocumentFile(context_, doc, node->nestedDocHref_, /*revealOnly=*/false);
    ui::EndDisabled();
    if (!exists)
        ui::TextDisabled("(nested file not found)");

    ui::Spacing();
    if (ui::Button(ICON_FA_TRASH " Delete Link"))
    {
        doc->DeleteNode(node); // routed to the text-level head removal
        return; // the model was rebuilt
    }
}

void UIViewInspector::RenderHeadLink(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc)
        return;

    const ea::string type = node->GetAttribute("type");
    const ea::string href = node->GetAttribute("href");
    const bool isTemplate = type == "text/template";

    ui::Text(ICON_FA_LINK " %s", isTemplate ? "RML Template Link" : "Stylesheet Link");
    ui::TextDisabled(ICON_FA_FILE " %s", doc->GetSourcePath().c_str());
    ui::Separator();

    const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), href);
    const bool exists = !resPath.empty() && ResourceExists(context_, resPath);

    // The two link kinds RmlUi supports differ in what the href points at: a
    // stylesheet dresses the widgets, a template provides the window chrome
    // that <body template="..."> instantiates. Switching rewrites the type
    // attribute of the same <link> element.
    static const char* kLinkTypes[] = {"text/rcss", "text/template"};
    int current = -1;
    if (type == "text/rcss")
        current = 0;
    else if (isTemplate)
        current = 1;
    int pendingType = -1;
    if (ui::BeginCombo("type", current >= 0 ? kLinkTypes[current]
        : (type.empty() ? "(no type)" : type.c_str())))
    {
        for (int i = 0; i < 2; i++)
        {
            const bool isSelected = (current == i);
            if (ui::Selectable(i == 0 ? "Stylesheet (.rcss)" : "RML Template (.rml)", isSelected)
                && !isSelected)
                pendingType = i;
        }
        ui::EndCombo();
    }
    if (pendingType >= 0)
    {
        doc->EditHeadLink(node, kLinkTypes[pendingType], href);
        return; // the model was rebuilt; re-render from the new node
    }

    // Resource path, resolved like RmlUi resolves it (a leading '/' is
    // rooted at the project's Data directory).
    char hrefBuf[512];
    snprintf(hrefBuf, sizeof(hrefBuf), "%s", href.c_str());
    ui::PushItemWidth(-40.0f);
    const bool hrefEdited = ui::InputText("href", hrefBuf, sizeof(hrefBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    // A picker offers the same Data-rooted spelling the loader expects,
    // narrowed to the kind this link claims to be (stylesheet vs template).
    const auto pickedHref = ResourceBrowseWidget("##hlEditBrowse", context_, href, isTemplate ? "rml" : "rcss");
    if (hrefEdited || pickedHref)
    {
        const ea::string newHref = hrefEdited ? Trim(ea::string(hrefBuf)) : *pickedHref;
        if (!newHref.empty())
        {
            doc->EditHeadLink(node, type, newHref);
            return; // the model was rebuilt; re-render from the new node
        }
    }

    ui::Spacing();
    if (isTemplate)
        ui::TextWrapped("The window frame (title bar, close button, ...) is minted from "
            "this template file when <body template=\"...\"> names it.");
    else
        ui::TextWrapped("The linked stylesheet provides the looks of this document's "
            "widgets; rules authored below still override it.");

    ui::BeginDisabled(!exists);
    if (ui::Button(ICON_FA_MAGNIFYING_GLASS " Reveal in Resource Browser"))
        OpenNestedDocumentFile(context_, doc, href, /*revealOnly=*/true);
    if (isTemplate)
    {
        ui::SameLine();
        if (ui::Button(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open"))
            OpenNestedDocumentFile(context_, doc, href, /*revealOnly=*/false);
    }
    ui::EndDisabled();
    if (!exists)
        ui::TextDisabled("(linked file not found)");

    ui::Spacing();
    if (ui::Button(ICON_FA_TRASH " Delete Link"))
    {
        doc->DeleteNode(node); // routed to the text-level head removal
        return; // the model was rebuilt
    }
}

void UIViewInspector::RenderTemplates(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc)
        return;

    // Template-minted elements (window frames, title bars, close buttons) have
    // no source in THIS document - they are instantiated from the nested .rml
    // files referenced by <head> <link type="text/template">. Surface those
    // file paths here, with a jump-in button, so editing them is one click
    // instead of a resource-browser hunt.
    const ea::vector<ea::string> links = doc->GetModel().GetTemplateLinks();
    if (links.empty())
        return;

    if (!ui::CollapsingHeader(ICON_FA_CUBES " Nested Documents", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ui::TextDisabled(
        "Elements minted by a template (window frame, title bar, close button)\n"
        "live in the template file, not in this document.");

    for (const ea::string& href : links)
    {
        const ea::string resPath = ResolveRelativeResourcePath(doc->GetSourcePath(), href);
        const bool exists = !resPath.empty() && ResourceExists(context_, resPath);

        ui::TextUnformatted(href.c_str());
        ui::SameLine();
        ui::TextDisabled("-> %s", resPath.c_str());
        ui::SameLine();
        ui::BeginDisabled(!exists);
        if (ui::SmallButton(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE " Open"))
        {
            // Multi-document: opens the template alongside this document
            // (no graceful close of the current workspace).
            auto* project = context_->GetSubsystem<Project>();
            if (project)
                project->ProcessRequest(MakeShared<OpenResourceRequest>(context_, resPath).Get());
        }
        ui::EndDisabled();
    }
}

namespace
{

/// std::string copy of an eastl string (the paragraph unit is std-only).
std::string Std(const ea::string& s)
{
    return std::string(s.c_str(), s.length());
}

/// True when the node's computed white-space keeps newlines (the pre kinds):
/// such paragraphs already render raw \n as line breaks, so their raw editor
/// stays the WYSIWYG form and the structural <br/> editor is not offered.
bool ParagraphUsesPreWhitespace(const UiNode& node)
{
    if (node.dom_)
    {
        const Rml::Style::WhiteSpace ws = node.dom_->GetComputedValues().white_space();
        return ws == Rml::Style::WhiteSpace::Pre || ws == Rml::Style::WhiteSpace::Prewrap
            || ws == Rml::Style::WhiteSpace::Preline;
    }
    // No live projection: fall back to the authored inline declaration.
    return LowerCopy(node.GetStyle("white-space")).find("pre") == 0;
}

/// Read the editable paragraph shape (children are text runs and bare <br/>
/// only) into the editor's normalized lines. False for anything else.
bool TryGetParagraphLines(const UiNode& host, std::vector<std::string>& lines)
{
    std::vector<ParagraphChildState> children;
    children.reserve(host.children_.size());
    for (const SharedPtr<UiNode>& child : host.children_)
    {
        ParagraphChildState state;
        state.srcNode = child->srcNode_;
        state.isText = child->IsText();
        if (state.isText)
        {
            state.text = Std(child->text_);
        }
        else if (child->tag_ != "br" || !child->attributes_.empty() || !child->style_.empty()
            || !child->id_.empty() || !child->classes_.empty() || !child->children_.empty())
        {
            return false;
        }
        children.push_back(std::move(state));
    }
    if (children.empty())
        return false;
    return ParagraphLinesOfChildren(children, lines);
}

} // namespace

bool UIViewInspector::RenderVisibility(UiNode* node)
{
    UIViewTab* tab = owner_;
    UIViewDocument* doc = tab ? tab->GetDocument() : nullptr;
    if (!doc || node->IsText())
        return false;

    // Reflect the primary (Inspector) node, apply to the whole selection as
    // ONE undo step (see UIViewDocument::SetNodesVisible). The authored state
    // is inline display: none - the channel that removes the widget AND its
    // layout space, and the one game code drops to reveal it at runtime.
    bool visible = LowerCopy(Trim(node->GetStyle("display"))) != "none";
    if (ui::Checkbox(ICON_FA_EYE " Visible", &visible))
    {
        if (doc->SetNodesVisible(tab->GetSelectedNodes(), visible))
            return true; // the model was rebuilt; the node is dangling now
    }
    if (ui::IsItemHovered())
    {
        ui::SetTooltip("Hides the selected element(s) by authoring inline display: none -\n"
            "the widget and its subtree leave the layout entirely, the way game UI\n"
            "starts hidden until game code drops the declaration. Un-hiding restores\n"
            "the value recorded when it was hidden (e.g. display: flex on a wrapped\n"
            "row); with none recorded the declaration is dropped and the cascade\n"
            "decides. A hidden element has no canvas box to click - re-select it\n"
            "from the Hierarchy.");
    }
    return false;
}

bool UIViewInspector::RenderTextContent(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!tab || !tab->GetDocument())
        return false;

    // The visible text of a label/button lives on a #text model node. Expose a
    // single editable field when the selection is itself a text node, or an
    // element whose only child is a text node (the "Text"/"Button" widgets and
    // plain <div>text</div> all take this shape). Mixed containers are skipped.
    UiNode* textNode = nullptr;
    if (node->IsText())
        textNode = node;
    else if (node->children_.size() == 1 && node->children_[0]->IsText())
        textNode = node->children_[0].Get();

    // A <p> whose children are text runs and bare <br/> takes the structural
    // multi-line editor: one buffer line per run, and Enter covers a line break
    // by inserting a <br/> on commit (under white-space: normal a raw \n would
    // collapse to a space). pre-formatted paragraphs keep the raw editor -
    // their \n already renders as breaks, so that form is their WYSIWYG one.
    const UiDocumentModel& model = tab->GetDocument()->GetModel();
    UiNode* host = node->IsText() ? model.FindParent(node) : node;
    const bool paragraph = host && LowerCopy(host->tag_) == "p";
    std::vector<std::string> paraLines;
    std::string paraBuffer;
    bool structural = false;
    if (paragraph && !ParagraphUsesPreWhitespace(*host) && TryGetParagraphLines(*host, paraLines))
    {
        paraBuffer = JoinParagraphLines(paraLines);
        // The fixed editor buffer must hold the whole paragraph; longer ones
        // fall back rather than committing a truncated rewrite.
        structural = paraBuffer.size() + 1 <= sizeof(contentBuf_);
    }

    if (!textNode && !structural)
        return false;

    if (!ui::CollapsingHeader(ICON_FA_FONT " Content", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    if (structural)
    {
        // Same seed-per-selection + explicit-Apply model as the raw inline
        // style editor: ImGui keeps its own buffer while the field is live, so
        // the lines are only re-read when the selection changes or a rebuild
        // invalidated the cache.
        const ea::vector<unsigned> curPath = tab->GetSelectedPath();
        if (!contentSeedValid_ || curPath != lastContentPath_)
        {
            snprintf(contentBuf_, sizeof(contentBuf_), "%s", paraBuffer.c_str());
            lastContentPath_ = curPath;
            contentSeedValid_ = true;
        }

        ui::InputTextMultiline("##textContent", contentBuf_, sizeof(contentBuf_), ImVec2(-1.0f, 120.0f));
        if (ui::Button(ICON_FA_CHECK " Apply Content"))
        {
            contentSeedValid_ = false;
            // An untouched buffer commits nothing (no edit, no undo step).
            if (NormalizedParagraphLines(contentBuf_) != paraLines)
            {
                tab->GetDocument()->SetParagraphText(host, ea::string(contentBuf_));
                return true; // the model was rebuilt; the nodes are dangling now
            }
        }
        return false;
    }

    if (!textNode)
        return false;

    if (paragraph)
    {
        // A pre-formatted paragraph keeps the raw multi-line editor: its \n is
        // rendered as authored, so the text is edited verbatim.
        if (textNode->text_.length() + 1 > sizeof(contentBuf_))
        {
            ui::TextDisabled("Text is too long for the inline editor.");
            return false;
        }
        // Same seed-per-selection + explicit-Apply model as the structural
        // editor above.
        const ea::vector<unsigned> curPath = tab->GetSelectedPath();
        if (!contentSeedValid_ || curPath != lastContentPath_)
        {
            snprintf(contentBuf_, sizeof(contentBuf_), "%s", textNode->text_.c_str());
            lastContentPath_ = curPath;
            contentSeedValid_ = true;
        }

        ui::InputTextMultiline("##textContent", contentBuf_, sizeof(contentBuf_), ImVec2(-1.0f, 120.0f));
        if (ui::Button(ICON_FA_CHECK " Apply Content"))
        {
            contentSeedValid_ = false;
            // An untouched buffer commits nothing (no edit, no undo step).
            if (ea::string(contentBuf_) != textNode->text_)
            {
                UiNodePayload payload = SnapshotUiNodePayload(*textNode);
                payload.text_ = ea::string(contentBuf_);
                tab->GetDocument()->EditNodePayload(textNode, payload);
                return true; // the model was rebuilt; the node is dangling now
            }
        }
        return false;
    }

    // Everything else keeps the single-line field: same seed-per-frame +
    // commit-on-deactivate model as the id/class rows - ImGui keeps its own
    // edit buffer while focused, so re-seeding is safe.
    char textBuf[1024];
    snprintf(textBuf, sizeof(textBuf), "%s", textNode->text_.c_str());
    ui::PushItemWidth(-1.0f);
    ui::InputText("##textContent", textBuf, sizeof(textBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    if (ui::IsItemDeactivatedAfterEdit())
    {
        UiNodePayload payload = SnapshotUiNodePayload(*textNode);
        payload.text_ = ea::string(textBuf);
        tab->GetDocument()->EditNodePayload(textNode, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

// ---------------------------------------------------------------------------
// Element attributes the engine itself reads
// ---------------------------------------------------------------------------
// The Inspector renders these as fixed rows, visible whether or not the
// document happens to carry them: an attribute that exists but has no row is
// invisible, and an invented attribute with an engine-looking name reads as a
// control that silently does nothing. Every row below is backed by the source
// line that reads it (RmlUi Source/Core/Elements/*).
//
// Deliberately absent - things this RmlUi build does not read: input
// placeholder/size/focus/autofocus (placeholder does not exist in the library
// at all), form action/method/enctype (ElementForm reads no attribute
// whatsoever), button disabled/name (it is not a control: CoreData/UI/
// layout.rcss styles button[disabled] but the engine never tests it), and the
// img width/height attributes, which would fight the style declarations of the
// same name. Also absent: data-* and on* - those are the binding and event
// surfaces and get designed as a unit elsewhere.
enum class AttrKind
{
    Text, ///< free-form string
    Decimal, ///< number; the engine clamps, this is only a typed-input filter
    Flag, ///< presence-only: ticked writes the attribute, unticked removes it
    Enum, ///< one of a fixed keyword set
    IdRef, ///< one of the ids present in the document (label's for)
};

struct AttrRow
{
    const char* tag;
    const char* type; ///< input only: the type it applies to; null = any type
    const char* name;
    AttrKind kind;
    const char* const* keywords; ///< Enum only, null-terminated
    const char* browseFilter; ///< non-null: file-picker button narrowed by this NFD spec ("png,jpg")
};

// Raster formats RmlUi's image decoder can load through the engine's Image.
const char* const kImageFilter = "png,jpg,jpeg,tga,bmp,webp,gif,dds";

const char* const kInputTypes[] = { "text", "password", "checkbox", "radio", "range", "submit", "button", nullptr };
const char* const kTextAreaWrap[] = { "normal", "free", nullptr };
const char* const kProgressDirections[] = { "left", "up", nullptr };

const AttrRow kAttrRows[] = {
    // src is a file under Data, so it gets a picker. sprite is NOT a path: the
    // engine resolves it as a name against sprites declared in loaded RCSS
    // (style_sheet->GetSprite), and rect is four numbers - neither is a file.
    { "img", nullptr, "src", AttrKind::Text, nullptr, kImageFilter },
    { "img", nullptr, "sprite", AttrKind::Text, nullptr },
    { "img", nullptr, "rect", AttrKind::Text, nullptr },

    { "input", nullptr, "type", AttrKind::Enum, kInputTypes },
    { "input", "text", "value", AttrKind::Text, nullptr },
    { "input", "text", "name", AttrKind::Text, nullptr },
    { "input", "text", "disabled", AttrKind::Flag, nullptr },
    { "input", "password", "value", AttrKind::Text, nullptr },
    { "input", "password", "name", AttrKind::Text, nullptr },
    { "input", "password", "disabled", AttrKind::Flag, nullptr },
    // checked is presence-only in the engine (InputTypeCheckbox tests
    // HasAttribute), so a hand-written checked="false" means CHECKED. That is
    // why this row is a tick box and never writes a literal true/false.
    { "input", "checkbox", "checked", AttrKind::Flag, nullptr },
    { "input", "checkbox", "value", AttrKind::Text, nullptr },
    { "input", "checkbox", "name", AttrKind::Text, nullptr },
    { "input", "checkbox", "disabled", AttrKind::Flag, nullptr },
    // name is what groups radios; without it each one stands alone.
    { "input", "radio", "checked", AttrKind::Flag, nullptr },
    { "input", "radio", "name", AttrKind::Text, nullptr },
    { "input", "radio", "value", AttrKind::Text, nullptr },
    { "input", "radio", "disabled", AttrKind::Flag, nullptr },
    { "input", "range", "value", AttrKind::Decimal, nullptr },
    { "input", "range", "min", AttrKind::Decimal, nullptr },
    { "input", "range", "max", AttrKind::Decimal, nullptr },
    { "input", "range", "step", AttrKind::Decimal, nullptr },
    { "input", "range", "disabled", AttrKind::Flag, nullptr },
    // submit/button: value is submitted with the form but never drawn (an
    // input of these types has no render path) - the visible label is a text
    // child, which the Text Content section edits.
    { "input", "submit", "name", AttrKind::Text, nullptr },
    { "input", "submit", "value", AttrKind::Text, nullptr },
    { "input", "submit", "disabled", AttrKind::Flag, nullptr },
    { "input", "button", "name", AttrKind::Text, nullptr },
    { "input", "button", "value", AttrKind::Text, nullptr },
    { "input", "button", "disabled", AttrKind::Flag, nullptr },

    { "select", nullptr, "value", AttrKind::Text, nullptr },
    { "select", nullptr, "name", AttrKind::Text, nullptr },
    { "select", nullptr, "disabled", AttrKind::Flag, nullptr },

    { "textarea", nullptr, "value", AttrKind::Text, nullptr },
    { "textarea", nullptr, "rows", AttrKind::Decimal, nullptr },
    { "textarea", nullptr, "cols", AttrKind::Decimal, nullptr },
    { "textarea", nullptr, "wrap", AttrKind::Enum, kTextAreaWrap },
    { "textarea", nullptr, "maxlength", AttrKind::Decimal, nullptr },
    { "textarea", nullptr, "name", AttrKind::Text, nullptr },
    { "textarea", nullptr, "disabled", AttrKind::Flag, nullptr },

    { "progress", nullptr, "value", AttrKind::Decimal, nullptr },
    { "progress", nullptr, "max", AttrKind::Decimal, nullptr },
    { "progress", nullptr, "direction", AttrKind::Enum, kProgressDirections },

    { "label", nullptr, "for", AttrKind::IdRef, nullptr },
};

constexpr size_t kNoAttr = static_cast<size_t>(-1);

size_t FindAttrIndex(const ea::vector<ea::pair<ea::string, ea::string>>& attributes, const ea::string& name)
{
    for (size_t i = 0; i < attributes.size(); ++i)
    {
        if (attributes[i].first == name)
            return i;
    }
    return kNoAttr;
}

void SetPayloadAttr(UiNodePayload& payload, const char* name, const ea::string& value)
{
    for (auto& attribute : payload.attributes_)
    {
        if (attribute.first == name)
        {
            attribute.second = value;
            return;
        }
    }
    payload.attributes_.emplace_back(name, value);
    // Keep the model's documented invariant (attributes sorted by name); this
    // only orders the payload vector, never the source text - a new attribute
    // is patched onto the element's own open tag wherever it sorts.
    ea::sort(payload.attributes_.begin(), payload.attributes_.end(),
        [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
        { return a.first < b.first; });
}

void DropPayloadAttr(UiNodePayload& payload, const ea::string& name)
{
    const size_t at = FindAttrIndex(payload.attributes_, name);
    if (at != kNoAttr)
        payload.attributes_.erase(payload.attributes_.begin() + at);
}

// The type an <input> presents to the engine: an absent attribute means text.
ea::string InputTypeOf(const UiNode& node)
{
    if (node.tag_ != "input")
        return ea::string();
    const size_t at = FindAttrIndex(node.attributes_, "type");
    const ea::string type = at != kNoAttr ? node.attributes_[at].second : ea::string("text");
    return LowerCopy(type);
}

void CollectIds(const UiNode& node, ea::vector<ea::string>& out)
{
    if (!node.id_.empty())
        out.push_back(node.id_);
    for (const SharedPtr<UiNode>& child : node.children_)
    {
        if (child)
            CollectIds(*child, out);
    }
}

bool UIViewInspector::RenderAttributes(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_LIST " Attributes", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    // Edits accumulate into a payload COPY; the model node is only touched by
    // the undoable command, which snapshots the pristine "old" state itself.
    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;

    // id / class are dedicated fields; editing them is structural (emitted).
    // Same row layout as the attribute rows below (name left, input right):
    // a bare InputText("id") would put ImGui's label after the field and the
    // section would read as two misaligned tables.
    char idBuf[256];
    snprintf(idBuf, sizeof(idBuf), "%s", node->id_.c_str());
    ui::Text("id");
    ui::SameLine();
    ui::PushItemWidth(-40.0f);
    ui::InputText("##id", idBuf, sizeof(idBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    if (ui::IsItemDeactivatedAfterEdit())
    {
        payload.id_ = Trim(idBuf);
        structural = true;
    }
    char classBuf[256];
    snprintf(classBuf, sizeof(classBuf), "%s", node->classes_.c_str());
    ui::Text("class");
    ui::SameLine();
    ui::PushItemWidth(-40.0f);
    ui::InputText("##class", classBuf, sizeof(classBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ui::PopItemWidth();
    if (ui::IsItemDeactivatedAfterEdit())
    {
        payload.classes_ = Trim(classBuf);
        structural = true;
    }

    // Structured rows the engine understands for this element, rendered whether
    // or not the document carries them: an attribute that exists but has no row
    // is invisible, and a row that only appears once the attribute exists makes
    // the affordance depend on how the element was born. Typing a value writes
    // the attribute and clearing the field drops it, so panel and text stay in
    // step whichever was edited first. A presence flag (checked/disabled) is a
    // tick box: the engine only tests that the attribute is there, so it is
    // written bare ("") and never as ="true"/"false" - the latter would flip a
    // checkbox's meaning, since checked="false" still counts as present.
    const ea::string inputType = InputTypeOf(*node);
    ea::vector<ea::string> rootIds;
    if (tab && tab->GetDocument() && tab->GetDocument()->GetModel().root_)
        CollectIds(*tab->GetDocument()->GetModel().root_, rootIds);

    ea::vector<ea::string> covered;
    for (const AttrRow& row : kAttrRows)
    {
        if (node->tag_ != row.tag || (row.type && inputType != row.type))
            continue;
        covered.push_back(row.name);

        const size_t at = FindAttrIndex(payload.attributes_, row.name);
        const bool present = at != kNoAttr;
        const ea::string value = present ? payload.attributes_[at].second : ea::string();

        ui::PushID(row.name);
        ui::Text("%s", row.name);
        ui::SameLine();
        switch (row.kind)
        {
        case AttrKind::Flag:
        {
            bool on = present; // presence is the entire state
            if (ui::Checkbox("##flag", &on))
            {
                if (on)
                    SetPayloadAttr(payload, row.name, ea::string());
                else
                    DropPayloadAttr(payload, row.name);
                structural = true;
            }
            break;
        }

        case AttrKind::Enum:
        case AttrKind::IdRef:
        {
            // Slot 0 lets the engine's own default stand by dropping the
            // attribute; the rest are the fixed keywords, or every id in the
            // document for a reference (label's for).
            ea::vector<ea::string> items;
            items.push_back(row.kind == AttrKind::IdRef ? ea::string("(none)") : ea::string("(default)"));
            if (row.kind == AttrKind::Enum)
            {
                for (const char* const* kw = row.keywords; kw && *kw; ++kw)
                    items.push_back(ea::string(*kw));
            }
            else
            {
                for (const ea::string& id : rootIds)
                    items.push_back(id);
            }
            int current = 0;
            if (present)
            {
                for (int i = 1; i < static_cast<int>(items.size()); ++i)
                {
                    if (items[i] == value)
                    {
                        current = i;
                        break;
                    }
                }
            }
            // A hand-written value outside the list still previews verbatim, so
            // the row never lies about what the text carries.
            const ea::string preview = present ? value : items[0];
            ui::PushItemWidth(-40.0f);
            if (ui::BeginCombo("##value", preview.c_str()))
            {
                for (int i = 0; i < static_cast<int>(items.size()); ++i)
                {
                    if (ui::Selectable(items[i].c_str(), i == current))
                    {
                        if (i == 0)
                        {
                            if (present)
                                DropPayloadAttr(payload, row.name);
                        }
                        else
                        {
                            SetPayloadAttr(payload, row.name, items[i]);
                        }
                        structural = true;
                    }
                }
                ui::EndCombo();
            }
            ui::PopItemWidth();
            break;
        }

        default: // Text / Decimal: a free field, Decimal filtered to numbers
        {
            char valBuf[1024];
            snprintf(valBuf, sizeof(valBuf), "%s", value.c_str());
            const ImGuiInputTextFlags extra =
                row.kind == AttrKind::Decimal ? ImGuiInputTextFlags_CharsDecimal : static_cast<ImGuiInputTextFlags>(0);
            ui::PushItemWidth(-40.0f);
            if (ui::InputText("##value", valBuf, sizeof(valBuf), extra | ImGuiInputTextFlags_EnterReturnsTrue))
            {
                const ea::string next = Trim(valBuf);
                if (next.empty())
                {
                    if (present)
                    {
                        DropPayloadAttr(payload, row.name);
                        structural = true;
                    }
                }
                else if (!present || next != value)
                {
                    SetPayloadAttr(payload, row.name, next);
                    structural = true;
                }
            }
            ui::PopItemWidth();
            // A resource-path row gets a picker beside the free field. A pick
            // commits at once - it names a file that is really there, which a
            // half-typed path is not.
            if (row.browseFilter)
            {
                if (auto picked = ResourceBrowseWidget((ea::string("##browse_") + row.name).c_str(), context_, value, row.browseFilter))
                {
                    SetPayloadAttr(payload, row.name, *picked);
                    structural = true;
                }
            }
            break;
        }
        }
        ui::PopID();
    }

    // Attributes no structured row claims (data-*, on*, or anything a tag
    // carries outside kAttrRows): still editable and removable, so the panel
    // never silently drops text it does not understand. Located by name rather
    // than index, because the rows above may have reordered the payload.
    for (size_t i = 0; i < node->attributes_.size(); ++i)
    {
        const ea::string name = node->attributes_[i].first;
        bool handled = false;
        for (const ea::string& c : covered)
        {
            if (c == name)
            {
                handled = true;
                break;
            }
        }
        if (handled)
            continue;
        const size_t at = FindAttrIndex(payload.attributes_, name);
        if (at == kNoAttr)
            continue; // dropped earlier this frame
        char valBuf[1024];
        snprintf(valBuf, sizeof(valBuf), "%s", payload.attributes_[at].second.c_str());
        ui::PushID(name.c_str());
        ui::Text("%s", name.c_str());
        ui::SameLine();
        ui::PushItemWidth(-40.0f);
        if (ui::InputText("##value", valBuf, sizeof(valBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            payload.attributes_[at].second = valBuf;
            structural = true;
        }
        ui::PopItemWidth();
        ui::SameLine();
        if (ui::SmallButton(ICON_FA_TRASH))
        {
            DropPayloadAttr(payload, name);
            structural = true;
            ui::PopID();
            break; // re-snapshot next frame
        }
        ui::PopID();
    }

    // Add-new row.
    ui::PushID("__new__");
    ui::InputText("name##newAttrName", attributeKeyBuf_, sizeof(attributeKeyBuf_));
    ui::SameLine();
    ui::PushItemWidth(-40.0f);
    ui::InputText("##newAttrValue", attributeValueBuf_, sizeof(attributeValueBuf_));
    ui::PopItemWidth();
    ui::SameLine();
    if (ui::SmallButton(ICON_FA_PLUS) && !Trim(attributeKeyBuf_).empty())
    {
        payload.attributes_.emplace_back(Trim(attributeKeyBuf_), attributeValueBuf_);
        ea::sort(payload.attributes_.begin(), payload.attributes_.end(),
            [](const ea::pair<ea::string, ea::string>& a, const ea::pair<ea::string, ea::string>& b)
            { return a.first < b.first; });
        structural = true;
        attributeKeyBuf_[0] = '\0';
        attributeValueBuf_[0] = '\0';
    }
    ui::PopID();

    if (structural && tab && tab->GetDocument())
    {
        tab->GetDocument()->EditNodePayload(node, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

// ---------------------------------------------------------------------------
// Inline style (RCSS) declarations the engine recognises
// ---------------------------------------------------------------------------
// Same discipline as kAttrRows, but this writes the style="..." channel
// (node->style_), never element attributes - the two do not cross (a src:
// typed here is a declaration for which RCSS has no property, and the engine
// drops it in silence). Every row is a property actually registered in RmlUi's
// StyleSheetSpecification, carrying its real default (shown as the field
// placeholder, never written down) and its inheritance flag. Values a row
// cannot represent - !important, or functional values like transform /
// decorator / animation, which get no row at all - are left to the raw editor
// below; a structured row only ever adds or removes the one declaration it
// edits, never disturbing the authored order or its siblings.
enum class StyleKind
{
    Keyword, ///< one of a fixed keyword set -> combo
    Length,  ///< <length>/<percentage> (or auto/none) -> free field
    Number,  ///< bare number -> number-filtered field
    Color,   ///< <color> -> free field
    Text,    ///< free string (font-family, cursor) -> free field
};

// The structured panel is an opinionated subset, not a CSS browser: only the
// few look properties worth a knob on every element get a row here (the layout
// kit has its own bespoke controls, see RenderLayout). Everything else RmlUi
// registers - borders, radius, the long tail of typography, box sizing, min/max,
// the rest of flex, ... - is left to the raw editor below; a structured row only
// ever adds or removes the one declaration it edits, never disturbing the
// authored order or its siblings.
struct StyleRow
{
    const char* name;
    const char* def;      ///< engine default: the placeholder; "" = no default text
    bool inherited;       ///< cascades to descendants
    StyleKind kind;
    const char* keywords; ///< Keyword only: ", "-separated, exactly as registered
};

const StyleRow kAppearanceRows[] = {
    { "background-color", "transparent", false, StyleKind::Color, nullptr },
    { "opacity", "1", true, StyleKind::Number, nullptr },
    { "color", "white", true, StyleKind::Color, nullptr },
    { "font-size", "12px", true, StyleKind::Length, nullptr },
};

int FindStyleIndexIn(const ea::vector<UiStyleDecl>& decls, const ea::string& name)
{
    for (int i = 0; i < static_cast<int>(decls.size()); ++i)
    {
        if (decls[i].name_ == name)
            return i;
    }
    return -1;
}

// Order-preserving, single-declaration edits (mirror UiNode::SetStyle/
// RemoveStyle but on the payload copy), so a structured row never disturbs the
// authored order or the declarations it does not own.
void SetPayloadStyle(UiNodePayload& payload, const ea::string& name, const ea::string& value)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    if (at >= 0)
        payload.style_[at].value_ = value;
    else
        payload.style_.push_back(UiStyleDecl{name, value});
}

void DropPayloadStyle(UiNodePayload& payload, const ea::string& name)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    if (at >= 0)
        payload.style_.erase(payload.style_.begin() + at);
}

// Split a registered keyword list (", "-separated, straight from the source)
// into trimmed items. Hand-rolled rather than via a split() so it depends on
// nothing but find/substr.
void SplitKeywords(const char* text, ea::vector<ea::string>& out)
{
    const ea::string s(text);
    size_t begin = 0;
    while (begin <= s.length())
    {
        const size_t comma = s.find(',', begin);
        size_t b = begin;
        size_t e = comma == ea::string::npos ? s.length() : comma;
        while (b < e && (s[b] == ' ' || s[b] == '\t'))
            ++b;
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
            --e;
        if (e > b)
            out.push_back(s.substr(b, e - b));
        if (comma == ea::string::npos)
            break;
        begin = comma + 1;
    }
}

// display value that turns an element into a flex container / makes its children
// flex items (compared against an already lowercased, trimmed string).
bool IsFlexValue(const ea::string& display)
{
    return display == "flex" || display == "inline-flex";
}

// Render one structured style row (label + its control) against the payload
// copy. Keyword rows offer "(default)" as slot 0, which drops the declaration
// so the engine default stands; a hand-written value outside the list still
// previews verbatim so the row never lies. Length/Number/Color/Text rows are
// free fields (Number is digit-filtered); clearing one drops its declaration
// rather than writing the default. Nothing commits here - callers accumulate
// into one payload and EditNodePayload once (commit-then-return).
void RenderOneStyleRow(const StyleRow& row, UiNodePayload& payload, bool& structural)
{
    const int at = FindStyleIndexIn(payload.style_, row.name);
    const bool present = at >= 0;
    const ea::string value = present ? payload.style_[at].value_ : ea::string();

    ui::PushID(row.name);
    // Surface inheritance: a color or font on a container is the engine feeding
    // its whole subtree, not a per-node quirk.
    ui::Text("%s%s", row.name, row.inherited ? "  *" : "");
    if (row.inherited && ui::IsItemHovered())
        ui::SetTooltip("Inherited: descendants take this value unless they override it.");
    ui::SameLine();

    if (row.kind == StyleKind::Keyword)
    {
        ea::vector<ea::string> items;
        items.push_back("(default)"); // drops the declaration, engine default stands
        SplitKeywords(row.keywords, items);
        int current = 0;
        if (present)
        {
            for (int i = 1; i < static_cast<int>(items.size()); ++i)
            {
                if (items[i] == Trim(value))
                {
                    current = i;
                    break;
                }
            }
        }
        const ea::string preview = present ? value
            : (row.def[0] != '\0' ? ea::string(row.def) : ea::string("(default)"));
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##v", preview.c_str()))
        {
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
            {
                if (ui::Selectable(items[i].c_str(), i == current))
                {
                    if (i == 0)
                    {
                        if (present)
                            DropPayloadStyle(payload, row.name);
                    }
                    else
                    {
                        SetPayloadStyle(payload, row.name, items[i]);
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
    }
    else
    {
        char valBuf[256];
        snprintf(valBuf, sizeof(valBuf), "%s", value.c_str());
        const ImGuiInputTextFlags extra =
            row.kind == StyleKind::Number ? ImGuiInputTextFlags_CharsDecimal : static_cast<ImGuiInputTextFlags>(0);
        ui::PushItemWidth(-8.0f);
        if (ui::InputTextWithHint("##v", row.def[0] != '\0' ? row.def : "(none)",
                valBuf, sizeof(valBuf), extra | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const ea::string next = Trim(valBuf);
            if (next.empty())
            {
                if (present)
                {
                    DropPayloadStyle(payload, row.name);
                    structural = true;
                }
            }
            else if (!present || next != value)
            {
                SetPayloadStyle(payload, row.name, next);
                structural = true;
            }
        }
        ui::PopItemWidth();
    }
    ui::PopID();
}

// A combo bound to a single style property whose raw CSS values are mapped to
// friendlier display labels (the auto-layout kit's align pickers). Slot 0 is
// "(default)": choosing it drops the declaration so the engine default stands.
void LayoutCombo(UiNodePayload& payload, bool& structural, const char* label,
    const char* name, const char* const* displayLabels, const char* const* cssValues,
    int count, const char* defaultText)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    const bool present = at >= 0;
    const ea::string value = present ? Trim(payload.style_[at].value_) : ea::string();

    int current = 0; // 0 == (default); n+1 == cssValues[n]
    if (present)
    {
        current = -1;
        for (int i = 0; i < count; ++i)
        {
            if (value == cssValues[i])
            {
                current = i + 1;
                break;
            }
        }
    }
    const ea::string preview = present ? value
        : (defaultText && defaultText[0] ? ea::string(defaultText) : ea::string("(default)"));

    ui::PushID(label);
    ui::TextUnformatted(label);
    ui::SameLine();
    ui::PushItemWidth(-8.0f);
    if (ui::BeginCombo("##v", preview.c_str()))
    {
        if (ui::Selectable("(default)", current == 0))
        {
            if (present)
                DropPayloadStyle(payload, name);
            structural = true;
        }
        for (int i = 0; i < count; ++i)
        {
            if (ui::Selectable(displayLabels[i], current == i + 1))
            {
                SetPayloadStyle(payload, name, cssValues[i]);
                structural = true;
            }
        }
        ui::EndCombo();
    }
    ui::PopItemWidth();
    ui::PopID();
}

// One free length/keyword field (width / height). Text form, so "200px", "50%",
// "auto" all pass through untouched; clearing it drops the declaration.
void LayoutSizeField(UiNodePayload& payload, bool& structural, const char* label,
    const char* name, const char* defaultText)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    const bool present = at >= 0;
    const ea::string value = present ? payload.style_[at].value_ : ea::string();

    char buf[64];
    snprintf(buf, sizeof(buf), "%s", value.c_str());
    ui::PushID(label);
    ui::TextUnformatted(label);
    ui::SameLine();
    ui::PushItemWidth(-8.0f);
    if (ui::InputTextWithHint("##v", defaultText, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const ea::string next = Trim(buf);
        if (next.empty())
        {
            if (present)
            {
                DropPayloadStyle(payload, name);
                structural = true;
            }
        }
        else if (!present || next != value)
        {
            SetPayloadStyle(payload, name, next);
            structural = true;
        }
    }
    ui::PopItemWidth();
    ui::PopID();
}

// Which edge(s) an absolutely-positioned element measures from, on one axis.
// The anchor grid is the cross product of a horizontal and a vertical mode, so
// the highlighted cell always matches the authored top/right/bottom/left +
// width/height exactly - there is no "custom" state it could misreport.
enum AnchorAxis
{
    AnchorNear,    // pinned to the leading edge (top / left); size as authored
    AnchorStretch, // both edges pinned and size auto: resizes with the container
    AnchorFar,     // pinned to the trailing edge (bottom / right)
};

// The size declaration counts as "authored" only if present and not auto.
bool IsSizedAxis(const UiNodePayload& payload, const char* name)
{
    const int at = FindStyleIndexIn(payload.style_, name);
    return at >= 0 && Trim(payload.style_[at].value_) != "auto";
}

bool HasInset(const UiNodePayload& payload, const char* name)
{
    return FindStyleIndexIn(payload.style_, name) >= 0;
}

void ResolveAnchorAxes(const UiNodePayload& payload, AnchorAxis& horiz, AnchorAxis& vert)
{
    const bool l = HasInset(payload, "left"), r = HasInset(payload, "right");
    const bool t = HasInset(payload, "top"), b = HasInset(payload, "bottom");
    horiz = (l && r && !IsSizedAxis(payload, "width")) ? AnchorStretch : (!l && r ? AnchorFar : AnchorNear);
    vert = (t && b && !IsSizedAxis(payload, "height")) ? AnchorStretch : (!t && b ? AnchorFar : AnchorNear);
}

// Pin an edge, seeding a flush 0 when it had no authored value yet (the offset
// field sits right there to nudge).
void PinAnchorEdge(UiNodePayload& payload, const char* inset)
{
    if (!HasInset(payload, inset))
        SetPayloadStyle(payload, inset, "0px");
}

// Rewrite one axis to match the chosen mode. Only insets and an auto size are
// authored - never margin or transform - so anchoring keeps a single code path
// and the grid, not the gizmo, owns non-top/left placement.
void ApplyAnchorAxis(UiNodePayload& payload, AnchorAxis axis,
    const char* nearEdge, const char* farEdge, const char* size)
{
    switch (axis)
    {
    case AnchorNear:
        PinAnchorEdge(payload, nearEdge);
        DropPayloadStyle(payload, farEdge);
        break;
    case AnchorFar:
        PinAnchorEdge(payload, farEdge);
        DropPayloadStyle(payload, nearEdge);
        break;
    case AnchorStretch:
        PinAnchorEdge(payload, nearEdge);
        PinAnchorEdge(payload, farEdge);
        DropPayloadStyle(payload, size); // auto size is what makes it stretch
        break;
    }
}

bool UIViewInspector::RenderAppearance(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_PAINT_ROLLER " Appearance", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;
    for (const StyleRow& row : kAppearanceRows)
        RenderOneStyleRow(row, payload, structural);

    // --- Background image: a single-value picker that authors the whole
    // `decorator` shorthand as one image(...). RmlUi has no background-image;
    // the decorator channel is what paints behind an element's content. Only
    // the "one image, no siblings" shape is editable here - mixed decorators
    // (image + gradient/box/tiled, or with an inner ')') render read-only so
    // we never silently drop what we cannot represent. State variants
    // (:hover/:active) need a .rcss selector, which inline style cannot carry;
    // those stay raw too.
    {
        const int at = FindStyleIndexIn(payload.style_, "decorator");
        const ea::string dec = at >= 0 ? Trim(payload.style_[at].value_) : ea::string();

        ea::string display;
        bool editable = true;
        const ea::string pfx = "image(";
        bool singleImage = false;
        ea::string inner;
        if (dec.size() >= pfx.size() + 2 && dec.compare(0, pfx.size(), pfx) == 0 && dec.back() == ')')
        {
            inner = Trim(dec.substr(pfx.size(), dec.size() - pfx.size() - 1));
            if (inner.find(')') == ea::string::npos)
                singleImage = true;
        }
        if (at >= 0 && !singleImage)
        {
            editable = false;
            display = dec; // show the authored string verbatim so the row does not lie
        }
        else if (singleImage)
        {
            if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\'') && inner.front() == inner.back())
                display = inner.substr(1, inner.size() - 2);
            else
                display = inner;
        }
        // else: no decorator authored at all -> empty editable field

        ui::TextUnformatted("Background image");
        if (ui::IsItemHovered())
        {
            if (editable)
                ui::SetTooltip("Paints a single image(...) behind this element's content via the\n"
                    "decorator channel (RmlUi has no background-image). Type a '/'-rooted path\n"
                    "under Data/ or a sprite name, or use the folder button. For image-fit,\n"
                    "mixed decorators, or :hover/:active variants, use Inline Style raw or a\n"
                    ".rcss rule (inline style carries no selectors).");
            else
                ui::SetTooltip("This element's decorator is not a single image(...) - it is\n"
                    "layered with something else. Edit it in Inline Style raw so the\n"
                    "other decorators survive.");
        }
        ui::SameLine();
        ui::PushItemWidth(-40.0f);
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", display.c_str());
        const char* hint = editable ? "path or sprite name" : "(mixed - edit in raw)";
        const bool submit = ui::InputTextWithHint("##bgimg", hint, buf, sizeof(buf),
            ImGuiInputTextFlags_EnterReturnsTrue
                | (editable ? 0 : ImGuiInputTextFlags_ReadOnly));
        ui::PopItemWidth();
        ea::optional<ea::string> picked;
        ui::BeginDisabled(!editable);
        picked = ResourceBrowseWidget("##bgBrowse", context_, display, kImageFilter);
        ui::EndDisabled();
        if (editable && (submit || picked))
        {
            const ea::string next = picked ? *picked : Trim(buf);
            if (next.empty())
            {
                if (at >= 0)
                {
                    DropPayloadStyle(payload, "decorator");
                    structural = true;
                }
            }
            else
            {
                // Author the argument UNQUOTED, exactly as RmlUi's own theme does
                // (image(arrow-down), src: /Textures/x.png). Two reasons:
                //  * The decorator tokenizer keeps any quote chars inside image(...)
                //    verbatim as part of the name, so a quoted src never resolves to a
                //    texture/sprite. Bare is the only form that actually renders.
                //  * The whole style value sits in a double-quoted style="..." attribute,
                //    so emitting a double quote would close that attribute early and
                //    corrupt the document (and our own byte-span re-parse of it).
                // The parenthesis state protects whitespace and commas from splitting, so
                // '/'-paths and sprite+orientation pairs are all fine written bare.
                const ea::string write = "image(" + next + ")";
                if (at < 0 || write != dec)
                {
                    SetPayloadStyle(payload, "decorator", write);
                    structural = true;
                }
            }
        }
    }

    if (structural && tab && tab->GetDocument())
    {
        // A structured commit changes style_; force the raw editor below to
        // reseed from the rebuilt model instead of showing its stale buffer.
        styleSeedValid_ = false;
        tab->GetDocument()->EditNodePayload(node, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

// The auto-layout kit: one opinionated, intent-named set of controls that each
// write the correct cluster of standard style declarations (a control can set or
// drop several). Everything is read back off the authored style_, so the panel
// never carries state the document does not. Long-tail properties are left raw.
bool UIViewInspector::RenderLayout(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_TABLE_CELLS " Layout", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    UiNodePayload payload = SnapshotUiNodePayload(*node);
    bool structural = false;

    // Read current authored values off the payload copy so the controls reflect
    // what is set, independent of the model about to be rebuilt.
    auto cur = [&](const char* name) -> ea::string
    {
        const int at = FindStyleIndexIn(payload.style_, name);
        return at >= 0 ? Trim(payload.style_[at].value_) : ea::string();
    };

    // --- Positioning: per-element, written as the standard `position`
    // declaration. The gizmo keys off absolute; relative makes this the anchor
    // for absolutely-positioned descendants without leaving the flow. ---
    {
        const ea::string pos = LowerCopy(cur("position"));
        int mode = (pos == "absolute") ? 2 : (pos == "relative") ? 1 : 0;
        const char* modes[] = { "In flow", "Anchor", "Free (absolute)" };
        ui::Text(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Position");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##pos", modes[mode]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ui::Selectable(modes[i], i == mode))
                {
                    if (i == 0)
                    {
                        // Back to flow: pull the positioning nail and its
                        // offsets, but keep the authored size (deliberate).
                        DropPayloadStyle(payload, "position");
                        DropPayloadStyle(payload, "top");
                        DropPayloadStyle(payload, "right");
                        DropPayloadStyle(payload, "bottom");
                        DropPayloadStyle(payload, "left");
                    }
                    else
                    {
                        SetPayloadStyle(payload, "position", i == 1 ? "relative" : "absolute");
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
        if (ui::IsItemHovered())
            ui::SetTooltip("In flow: laid out by its container. Anchor: stays in flow but is the\n"
                "reference for absolutely-positioned descendants. Free: out of flow, draggable\n"
                "in the preview against the nearest Anchor/Free ancestor.");
    }

    // --- Z-order: stacking within a positioned context. Always visible (a common
    // HUD/overlay/modal need), not gated on position. Empty = auto (drops the
    // declaration); a number - positive or negative - is written verbatim.
    LayoutSizeField(payload, structural, "Z-order", "z-index", "auto");
    if (ui::IsItemHovered())
        ui::SetTooltip("Stacking order among siblings in the same context. CSS honours it\n"
            "only once the element is positioned: leave it empty for the document default,\n"
            "or set Position to Anchor (relative) / Free (absolute) for it to take effect.");

    // --- Anchor (absolutely-positioned elements only): pick which edges this box
    // pins to and whether it stretches with its container. The 3x3 grid is the
    // cross product of the two per-axis modes, so a cell is always highlighted to
    // match the authored insets + size. The gizmo only ever writes top/left, so
    // re-pick a cell after dragging to re-seat on another edge. ---
    if (LowerCopy(cur("position")) == "absolute")
    {
        AnchorAxis h = AnchorNear, v = AnchorNear;
        ResolveAnchorAxes(payload, h, v);

        ui::Text("Anchor");
        const char* cellTip[3][3] = {
            { "top-left corner", "top edge, stretch across", "top-right corner" },
            { "left edge, stretch down", "fill the container", "right edge, stretch down" },
            { "bottom-left corner", "bottom edge, stretch across", "bottom-right corner" },
        };
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const AnchorAxis ch = col == 0 ? AnchorNear : col == 1 ? AnchorStretch : AnchorFar;
                const AnchorAxis cv = row == 0 ? AnchorNear : row == 1 ? AnchorStretch : AnchorFar;
                ui::PushID(row * 3 + col);
                if (ui::RadioButton("##a", h == ch && v == cv))
                {
                    ApplyAnchorAxis(payload, ch, "left", "right", "width");
                    ApplyAnchorAxis(payload, cv, "top", "bottom", "height");
                    structural = true;
                }
                if (ui::IsItemHovered())
                    ui::SetTooltip("%s", cellTip[row][col]);
                ui::PopID();
                if (col < 2)
                    ui::SameLine(0.0f, 24.0f);
            }
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Middle row / column stretch the box on that axis (size becomes\n"
                "auto). Centering a small element on both axes is left to a flex parent\n"
                "(Align) or raw margin:auto.");

        ui::TextDisabled("Insets (offset from each pinned edge; px or %)");
        LayoutSizeField(payload, structural, "Top", "top", "auto");
        LayoutSizeField(payload, structural, "Right", "right", "auto");
        LayoutSizeField(payload, structural, "Bottom", "bottom", "auto");
        LayoutSizeField(payload, structural, "Left", "left", "auto");
    }

    // --- Direction: picking Row/Column makes this a flex container (display:
    // flex), which also blocks-ifies its children so the inline-div trap cannot
    // bite. The container knobs below only apply once this is flex. ---
    {
        const bool flex = IsFlexValue(LowerCopy(cur("display")));
        const ea::string dir = LowerCopy(cur("flex-direction"));
        int d = flex ? ((dir == "column") ? 1 : 0) : -1;
        const char* dirs[] = { "(plain box)", "Row", "Column" };
        ui::Text(ICON_FA_UP_DOWN " Direction");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##dir", dirs[d < 0 ? 0 : d + 1]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ui::Selectable(dirs[i], i == (d < 0 ? 0 : d + 1)))
                {
                    if (i == 0)
                    {
                        DropPayloadStyle(payload, "display");
                        DropPayloadStyle(payload, "flex-direction");
                    }
                    else
                    {
                        SetPayloadStyle(payload, "display", "flex");
                        SetPayloadStyle(payload, "flex-direction", i == 1 ? "row" : "column");
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
        if (ui::IsItemHovered())
            ui::SetTooltip("Row / Column arranges this element's children. (plain box) leaves the\n"
                "display to the engine default (edit it or the -reverse variants via raw).");
    }

    // The flex container knobs and child sizing stay visible but are disabled
    // outside their context, so the panel does not reshuffle as you edit.
    const bool isFlex = IsFlexValue(LowerCopy(cur("display")));
    bool isFlexItem = false;
    if (tab && tab->GetDocument())
    {
        if (const UiNode* parent = tab->GetDocument()->GetModel().FindParent(node))
            isFlexItem = IsFlexValue(LowerCopy(Trim(parent->GetStyle("display"))));
    }

    ui::BeginDisabled(!isFlex);
    {
        // Gap: one field writes both axes (a single-axis stack ignores the other).
        {
            const ea::string gv = cur("row-gap");
            char buf[64];
            snprintf(buf, sizeof(buf), "%s", gv.c_str());
            ui::Text("Gap");
            ui::SameLine();
            ui::PushItemWidth(-8.0f);
            if (ui::InputTextWithHint("##gap", "0px", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                const ea::string next = Trim(buf);
                if (next.empty())
                {
                    DropPayloadStyle(payload, "row-gap");
                    DropPayloadStyle(payload, "column-gap");
                }
                else
                {
                    SetPayloadStyle(payload, "row-gap", next);
                    SetPayloadStyle(payload, "column-gap", next);
                }
                structural = true;
            }
            ui::PopItemWidth();
        }

        static const char* const jl[] = { "Start", "Center", "End", "Between", "Around" };
        static const char* const jv[] = { "flex-start", "center", "flex-end", "space-between", "space-around" };
        LayoutCombo(payload, structural, "Align (main axis)", "justify-content", jl, jv, 5, "flex-start");

        static const char* const al[] = { "Start", "Center", "End", "Stretch" };
        static const char* const av[] = { "flex-start", "center", "flex-end", "stretch" };
        LayoutCombo(payload, structural, "Align (cross axis)", "align-items", al, av, 4, "stretch");
    }
    ui::EndDisabled();

    // Scroll: one checkbox = overflow-y:auto + overflow-x:hidden. Deliberately
    // outside the flex gate - the engine's own #content scrolls a plain block box
    // (block + fixed height + overflow), the more reliable path. Flex is only
    // wanted for Gap/alignment; a simple list does not need it.
    {
        bool on = LowerCopy(cur("overflow-y")) == "auto";
        ui::Text("Scroll");
        ui::SameLine();
        if (ui::Checkbox(" vertically##scroll", &on))
        {
            if (on)
            {
                SetPayloadStyle(payload, "overflow-y", "auto");
                SetPayloadStyle(payload, "overflow-x", "hidden");
            }
            else
            {
                DropPayloadStyle(payload, "overflow-y");
                DropPayloadStyle(payload, "overflow-x");
            }
            structural = true;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Give the box a fixed Height and the rows a real size so their total\n"
                "exceeds it and scrolls. In a plain block box that is all that is needed; in a\n"
                "Row/Column container also pin the rows with Hug/Fill (flex-shrink:0) so they\n"
                "are not squeezed to fit.");
    }

    // Padding: one field writes all four sides (four-way tuning lives in raw).
    {
        const ea::string pv = cur("padding-top");
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", pv.c_str());
        ui::Text("Padding");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::InputTextWithHint("##pad", "0px", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const ea::string next = Trim(buf);
            if (next.empty())
            {
                DropPayloadStyle(payload, "padding-top");
                DropPayloadStyle(payload, "padding-right");
                DropPayloadStyle(payload, "padding-bottom");
                DropPayloadStyle(payload, "padding-left");
            }
            else
            {
                SetPayloadStyle(payload, "padding-top", next);
                SetPayloadStyle(payload, "padding-right", next);
                SetPayloadStyle(payload, "padding-bottom", next);
                SetPayloadStyle(payload, "padding-left", next);
            }
            structural = true;
        }
        ui::PopItemWidth();
    }

    // Margin: one field writes all four sides, mirroring Padding (four-way
    // tuning lives in raw). Adjacent vertical margins collapse per CSS, so
    // the rendered gap between neighbors can be smaller than the value here.
    {
        const ea::string mv = cur("margin-top");
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", mv.c_str());
        ui::Text("Margin");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::InputTextWithHint("##margin", "0px", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const ea::string next = Trim(buf);
            if (next.empty())
            {
                DropPayloadStyle(payload, "margin-top");
                DropPayloadStyle(payload, "margin-right");
                DropPayloadStyle(payload, "margin-bottom");
                DropPayloadStyle(payload, "margin-left");
            }
            else
            {
                SetPayloadStyle(payload, "margin-top", next);
                SetPayloadStyle(payload, "margin-right", next);
                SetPayloadStyle(payload, "margin-bottom", next);
                SetPayloadStyle(payload, "margin-left", next);
            }
            structural = true;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Adjacent vertical margins collapse (CSS): the rendered gap between\n"
                "neighbors is the larger of the two, not their sum. Four-way tuning\n"
                "lives in the raw style.");
        ui::PopItemWidth();
    }

    LayoutSizeField(payload, structural, "Width", "width", "auto");
    LayoutSizeField(payload, structural, "Height", "height", "auto");

    // Child sizing: meaningful only when this element is itself a flex item.
    ui::BeginDisabled(!isFlexItem);
    {
        const ea::string grow = cur("flex-grow");
        int s = -1; // 0 hug, 1 fill
        if (grow == "0")
            s = 0;
        else if (grow == "1")
            s = 1;
        const char* sl[] = { "(auto)", "Hug (content)", "Fill (parent)" };
        ui::Text("Size in parent");
        ui::SameLine();
        ui::PushItemWidth(-8.0f);
        if (ui::BeginCombo("##sizing", sl[s < 0 ? 0 : s + 1]))
        {
            for (int i = 0; i < 3; ++i)
            {
                if (ui::Selectable(sl[i], i == (s < 0 ? 0 : s + 1)))
                {
                    if (i == 0)
                    {
                        DropPayloadStyle(payload, "flex-grow");
                        DropPayloadStyle(payload, "flex-shrink");
                        DropPayloadStyle(payload, "flex-basis");
                    }
                    else
                    {
                        // Both pin flex-shrink:0 - the flex trap that otherwise
                        // squeezes rows to fit and kills scrolling.
                        SetPayloadStyle(payload, "flex-grow", i == 1 ? "0" : "1");
                        SetPayloadStyle(payload, "flex-shrink", "0");
                    }
                    structural = true;
                }
            }
            ui::EndCombo();
        }
        ui::PopItemWidth();
        if (ui::IsItemHovered())
            ui::SetTooltip("Available when this element's parent is a Row/Column container.");
    }
    ui::EndDisabled();

    if (structural && tab && tab->GetDocument())
    {
        // A structured commit changes style_; force the raw editor below to
        // reseed from the rebuilt model instead of showing its stale buffer.
        styleSeedValid_ = false;
        tab->GetDocument()->EditNodePayload(node, payload);
        return true; // the model was rebuilt; the node is dangling now
    }
    return false;
}

bool UIViewInspector::RenderInlineStyle(UiNode* node)
{
    UIViewTab* tab = owner_;
    if (!ui::CollapsingHeader(ICON_FA_CODE " Inline Style (raw)", ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    ui::TextDisabled("The full style=\"...\" list, verbatim. Use it for values the "
        "structured Style rows above cannot express (transform, decorator, "
        "animation, !important). Applying this replaces every declaration it lists.");

    const ea::vector<unsigned> curPath = tab ? tab->GetSelectedPath() : ea::vector<unsigned>{};
    if (!styleSeedValid_ || curPath != lastStylePath_)
    {
        // Seed from the model's ordered declarations (source of truth).
        ea::string text;
        for (const UiStyleDecl& decl : node->style_)
            text += decl.name_ + ": " + decl.value_ + ";\n";
        snprintf(styleBuf_, sizeof(styleBuf_), "%s", text.c_str());
        lastStylePath_ = curPath;
        styleSeedValid_ = true;
    }

    bool edited = false;
    ui::InputTextMultiline("##style", styleBuf_, sizeof(styleBuf_), ImVec2(-1.0f, 120.0f));
    if (ui::Button(ICON_FA_CHECK " Apply Style"))
        edited = true;
    if (edited)
    {
        ea::vector<UiStyleDecl> parsed;
        ParseStyleDeclarations(ea::string(styleBuf_), parsed);
        styleSeedValid_ = false;
        if (tab && tab->GetDocument())
        {
            UiNodePayload payload = SnapshotUiNodePayload(*node);
            payload.style_ = ea::move(parsed);
            tab->GetDocument()->EditNodePayload(node, payload);
            return true; // the model was rebuilt; the node is dangling now
        }
    }
    return false;
}

void UIViewInspector::RenderComputed(UiNode* node)
{
    if (!ui::CollapsingHeader(ICON_FA_CALCULATOR " Computed", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (node->dom_)
    {
        const Vector2 pos = V2(node->dom_->GetAbsoluteOffset(Rml::BoxArea::Border));
        const Vector2 size = V2(node->dom_->GetBox().GetSize(Rml::BoxArea::Border));
        ui::Text("left %s  top %s", FormatPx(pos.x_).c_str(), FormatPx(pos.y_).c_str());
        ui::Text("width %s  height %s", FormatPx(size.x_).c_str(), FormatPx(size.y_).c_str());
    }

    // The gizmo now keys off the position declaration alone, so surface that
    // here: an absolutely positioned node can be dragged/resized in the preview.
    if (node->GetStyle("position") == "absolute")
        ui::TextDisabled("(absolutely positioned - drag the handles in the preview to move or resize)");
}

}
