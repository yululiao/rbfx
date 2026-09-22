//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

#include "RmlTextModel.h"

#include <Urho3D/Urho3D.h>
#include <Urho3D/Container/Ptr.h>
#include <Urho3D/Math/Vector2.h>

#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace Rml
{
class Element;
class ElementDocument;
}

namespace Urho3D
{

/// One inline style declaration ("name: value") kept in authored order so that
/// emission is stable and diffs stay minimal.
struct UiStyleDecl
{
    ea::string name_;
    ea::string value_;
};

/// Editor-side document model node: the source of truth for the UI editor,
/// mirroring one RML element. The RmlUi DOM is only a runtime projection that
/// is regenerated from the emitted text on every edit.
struct UiNode : public RefCounted
{
    ea::string tag_; ///< "div"/"button"/... ; "#text" for raw text nodes
    ea::string text_; ///< Contents of "#text" nodes
    ea::string id_;
    ea::string classes_;
    ea::vector<ea::pair<ea::string, ea::string>> attributes_; ///< Sorted; excludes id/class/style
    ea::vector<UiStyleDecl> style_; ///< Authored order
    ea::vector<SharedPtr<UiNode>> children_;
    Rml::Element* dom_ = nullptr; ///< Runtime projection; rebuilt with the model on reload
    int srcNode_ = -1; ///< Index into UiDocumentModel::source_ this node was built from (-1 if editor-created)

    /// Virtual node standing for the nested .rml whose template mints the
    /// window chrome into the preview ("#nested-doc"). Exists only in the
    /// editor tree - it is never serialized into the document text.
    bool IsNestedDoc() const { return tag_ == "#nested-doc"; }
    /// Virtual node standing for one <link> element of <head> ("#head-link").
    /// Like the nested-doc node it exists only in the editor tree - never
    /// serialized, never DOM-correlated (no preview box to draw). Its type
    /// and href live as plain attributes so per-node editors can read them;
    /// edits route through the text-level head commands.
    bool IsHeadLink() const { return tag_ == "#head-link"; }
    /// Position of this link among <head>'s <link> elements in authored
    /// order; routes text-level edits to the right spine element.
    unsigned headLinkOrdinal_ = 0;
    /// Raw href of the <head> <link> this node stands for (as authored).
    ea::string nestedDocHref_;
    /// Direct children of the live document minted by the nested template
    /// (every direct child that does not host authored content: title bar,
    /// resize handles, and the content container itself is excluded). The
    /// selection outline is the union of their boxes. Runtime projection
    /// only, rebuilt together with the dom_ links on every reload.
    ea::vector<Rml::Element*> nestedChromeElems_;

    int FindStyle(const ea::string& name) const;
    ea::string GetStyle(const ea::string& name) const;
    /// Value of a plain attribute ("" when absent).
    ea::string GetAttribute(const ea::string& name) const;
    void SetStyle(const ea::string& name, const ea::string& value); ///< Replaces in place or appends
    void RemoveStyle(const ea::string& name);
    bool IsText() const { return tag_ == "#text"; }

    /// True when the node carries position:absolute + explicit px left/top/width/height
    /// (pure data check over style_; no DOM involved). Such nodes are gizmo-editable.
    bool IsMaterialized() const;
};

/// Undo payload: everything editable on a single node except its children
/// (tag stays immutable once created). Old/new payloads are diffed by the
/// undo actions and applied whole, so any subset of fields can change in one
/// recorded step.
struct UiNodePayload
{
    ea::string text_;
    ea::string id_;
    ea::string classes_;
    ea::vector<ea::pair<ea::string, ea::string>> attributes_;
    ea::vector<UiStyleDecl> style_;
};

/// Capture the editable payload of a node (children excluded).
UiNodePayload SnapshotUiNodePayload(const UiNode& node);
/// Overwrite a node's editable payload (children untouched).
void ApplyUiNodePayload(UiNode& node, const UiNodePayload& payload);

/// Deep copy of a node subtree (dom_ projection links are dropped).
SharedPtr<UiNode> DeepCloneUiNode(const UiNode& src);

/// Full editor model of one .rml document. Built by walking the loaded DOM and
/// serialized back via EmitRml().
struct UiDocumentModel
{
    SharedPtr<UiNode> root_; ///< "body" (maps to the ElementDocument itself)
    RmlTextModel source_; ///< The authoritative text spine: original bytes + located edit spans

    /// Seed the spine from the raw source text and build the editor tree from it
    /// (so {{bindings}}, data-* tokens, comments and authored order are preserved).
    /// Then attach the live preview \a document to each node's dom_ by structural
    /// correlation (elements match by tag/ordinal; exotic subtrees may stay null).
    bool BuildFromText(const ea::string& sourceText, Rml::ElementDocument* document);

    /// Serialize back to a complete .rml text by diffing the current tree against
    /// the spine and applying only the changed regions as span patches.
    ea::string EmitRml() const;

    UiNode* FindByDom(const Rml::Element* element) const;
    /// Parent of \a node in the model tree, or null (root / not found).
    UiNode* FindParent(const UiNode* node) const;
    /// Child-index path from the root to \a node. Returns false if not found.
    bool BuildPath(const UiNode* node, ea::vector<unsigned>& path) const;
    UiNode* ResolvePath(const ea::vector<unsigned>& path) const;

    /// hrefs of the <link type="text/template"> entries in <head>, in authored
    /// order. These reference the nested .rml files whose elements (window
    /// frames, close buttons) are minted into the live document but are not
    /// part of this document's own source. Callers resolve each href against
    /// the document's resource path (RmlUi's document-relative JoinPath).
    ea::vector<ea::string> GetTemplateLinks() const;

    /// Text-level edits of <head>'s <link> elements. <head> lives in the
    /// spine's untouched bytes (the editor tree starts at <body>), so unlike
    /// the tree commands these parse \a text into a THROWAWAY RmlTextModel -
    /// the live spine is never re-parsed or mutated here, that would
    /// invalidate every srcNode_ anchor - and return the edited document text
    /// via \a out.
    /// @{
    /// Insert <link type=\a type href=\a href> right after the last existing
    /// link (the authored order of the others stays put - rcss cascade order
    /// matters; in a link-less head it becomes the first child, so an inline
    /// <style> still loads after it and can override the sheet). False when
    /// the document has no <head>, or the (type, href) pair is empty/dup.
    bool InsertHeadLink(const ea::string& text, const ea::string& type,
        const ea::string& href, ea::string& out);
    /// Overwrite the type/href attributes of the \a ordinal'th link (in
    /// authored order). False when the ordinal is stale (fewer links exist).
    bool EditHeadLinkAt(const ea::string& text, unsigned ordinal, const ea::string& type,
        const ea::string& href, ea::string& out);
    /// Remove the \a ordinal'th link: the whole authored line, indent and
    /// newline included. False when the ordinal is stale.
    bool RemoveHeadLinkAt(const ea::string& text, unsigned ordinal, ea::string& out);
    /// @}

    /// The template-chrome virtual node (see UiNode::IsNestedDoc), or null
    /// when the document does not instantiate a nested template.
    UiNode* GetNestedDoc() const;
};

/// Trim leading/trailing whitespace.
ea::string Trim(const ea::string& s);
/// Parse "name: value; name2: value2" text into ordered declarations.
void ParseStyleDeclarations(const ea::string& text, ea::vector<UiStyleDecl>& out);
/// Format declarations back to "name: value; name2: value2" (no trailing ';').
ea::string FormatStyleDeclarations(const ea::vector<UiStyleDecl>& decls);

/// Parsed editor-emitted "transform" subset: rotate(Adeg) / scale(SX[,SY]).
struct UiTransform
{
    float rotateDeg_ = 0.0f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
};
UiTransform ParseUiTransform(const ea::string& value);
/// Empty string when identity (so the declaration can be dropped).
ea::string FormatUiTransform(const UiTransform& t);

/// Parses "120", "120px", " 120.5 PX" as 120.5f. Fails on %, em, etc.
bool TryParsePx(const ea::string& value, float& out);
/// Formats a CSS number without scientific notation ("120", "64.5").
ea::string FormatCssNumber(double value);
/// Formats a length as a stable px declaration value ("120px", "64.5px").
ea::string FormatPx(float value);

/// XML-escapes element text and attribute values for emission.
ea::string EscapeRmlText(const ea::string& s);

}
