//
// Copyright (c) 2017-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.
//

#pragma once

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
/// is regenerated from the emitted text on every structural change.
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

    int FindStyle(const ea::string& name) const;
    ea::string GetStyle(const ea::string& name) const;
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
    ea::string headRaw_; ///< "<head>...</head>" block taken verbatim from the source text

    /// Rebuild the model from a loaded & shown document.
    void BuildFromDom(Rml::ElementDocument* document);

    /// Serialize back to a complete .rml text. Deterministic: same model, same bytes.
    ea::string EmitRml() const;

    UiNode* FindByDom(const Rml::Element* element) const;
    /// Parent of \a node in the model tree, or null (root / not found).
    UiNode* FindParent(const UiNode* node) const;
    /// Child-index path from the root to \a node. Returns false if not found.
    bool BuildPath(const UiNode* node, ea::vector<unsigned>& path) const;
    UiNode* ResolvePath(const ea::vector<unsigned>& path) const;
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
