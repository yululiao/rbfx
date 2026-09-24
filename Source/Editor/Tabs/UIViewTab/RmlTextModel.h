//
// Pure-text RML/RCSS source model for the UI layout editor.
//
// Design contract (see editor positioning):
//   * The character buffer is the ONLY source of truth. Serialize() == the buffer, so an
//     untouched document round-trips byte-for-byte by construction.
//   * The parsed tree is a *recomputable index* over that buffer; it exists only to LOCATE
//     spans. Editing = splice a located span with new text, then re-parse. Nothing else moves.
//   * The editor never invents syntax: generated text is plain RmlUi RML/CSS.
//
// Deliberately dependency-free (std only) so it can be unit tested headless with a plain
// compiler AND linked into the editor unchanged. Layout is NOT done here; RmlUi renders this
// text elsewhere.
//
#pragma once

#include <string>
#include <vector>

enum class RmlNodeKind
{
    Root,        // synthetic document root (index 0)
    Element,     // <tag ...> ... </tag> or <tag .../>
    Text,        // character data between tags
    Comment,     // <!-- ... -->
    Raw,         // <?xml?>, <!DOCTYPE ...>, <![CDATA[...]]>, or a stray '<' that is not markup
};

// A byte range into the buffer. offset/length are indices into RmlTextModel::GetText().
struct RmlSpan
{
    int offset = 0;
    int length = 0;
    int End() const { return offset + length; }
};

// One "property: value" inside a style="" attribute. Spans are buffer-global.
struct RmlStyleDecl
{
    std::string property;
    std::string value;    // raw text as authored (trimmed)
    RmlSpan whole;        // the "property: value" run (trimmed)
    RmlSpan valueSpan;    // just the value token
    bool important = false;
};

struct RmlAttribute
{
    std::string name;
    std::string value;        // raw text between the quotes (empty if valueless)
    char quote = '"';         // '"' or '\''; 0 if unquoted or valueless
    bool isStyle = false;
    RmlSpan whole;            // the full name="value" run
    RmlSpan valueWithQuotes;  // "value" (including quote chars); == whole if unquoted
    RmlSpan valueSpan;        // characters between the quotes (length 0 if valueless)
    std::vector<RmlStyleDecl> styleDecls; // populated only when isStyle
};

struct RmlNode
{
    RmlNodeKind kind = RmlNodeKind::Root;
    int parent = -1;
    std::vector<int> children; // child node indices, in document order

    // Element-only:
    std::string tag;
    bool selfClosing = false;
    RmlSpan whole;    // <tag ...> ... </tag>  (or the whole self-closing tag)
    RmlSpan openTag;  // <tag ...> or <tag .../>
    RmlSpan nameSpan; // the tag name
    RmlSpan innerSpan;// content between '>' of open tag and '<' of close tag
    std::vector<RmlAttribute> attributes;

    // Text/Comment/Raw-only:
    std::string text; // raw characters
    RmlSpan span;     // byte range of this leaf
};

// A located edit: replace `span` with `replacement`. All patches in a batch are computed against
// the same parse, so their spans refer to consistent offsets until they are applied together.
struct RmlPatch
{
    RmlSpan span;
    std::string replacement;
};

class RmlTextModel
{
public:
    // Parse a whole document. Always succeeds and stores the text; returns false only if the
    // markup is structurally inconsistent in a way we could not index (unclosed tags are
    // tolerated and closed at EOF). GetText() returns the stored text verbatim regardless.
    bool Load(const std::string& text);

    const std::string& GetText() const { return text_; }

    int Root() const { return 0; }
    const RmlNode& Node(int index) const { return nodes_[index]; }
    int NodeCount() const { return static_cast<int>(nodes_.size()); }
    int CountElements() const;

    // First element node (pre-order) with the given tag name (case-insensitive); -1 if none.
    int FindFirstElement(const std::string& tag) const;

    // Resolve a node by a path of child indices from the synthetic root (all child kinds count).
    int ResolvePath(const std::vector<int>& path) const;
    int ChildIndexOf(int node) const;                        // index within parent's children
    int ElementChildAt(int parent, int ordinal) const;       // nth ELEMENT child -> node index / -1
    int ElementChildCount(int parent) const;

    const RmlAttribute* FindAttribute(int node, const std::string& name) const;

    // ---- Surgical edits. Each splices the buffer and re-parses. Node indices are invalid
    // after any successful edit; callers must re-resolve (by path or FindFirstElement). ----
    bool SetAttribute(int node, const std::string& name, const std::string& value);
    bool RemoveAttribute(int node, const std::string& name);
    bool SetStyleProperty(int node, const std::string& property, const std::string& value);
    bool RemoveStyleProperty(int node, const std::string& property);
    bool GetStyleProperty(int node, const std::string& property, std::string& outValue) const;

    // Insert generated markup as an element child of parent at position childOrdinal (0..count;
    // count == append at end of inner). Returns the new node index, or -1 on failure.
    int InsertElement(int parent, int childOrdinal, const std::string& markup);
    bool RemoveElement(int node);

    // ---- Patch collection against the CURRENT parse (no mutation) + batch apply. The editor's
    // save-time reconcile computes every edit from ONE stable parse (so all returned offsets are
    // mutually valid) then applies them in a single re-parse. Compute* return false if the target
    // is missing/invalid. On success `out.span` is a range of the current GetText(). ----
    bool ComputeAttributePatch(int node, const std::string& name, const std::string& value, RmlPatch& out) const;
    bool ComputeAttributeRemovalPatch(int node, const std::string& name, RmlPatch& out) const;
    bool ComputeStylePropertyPatch(int node, const std::string& property, const std::string& value, RmlPatch& out) const;
    bool ComputeStyleRemovePatch(int node, const std::string& property, RmlPatch& out) const;
    /// Patch replacing the element's entire inline style with `declarations`
    /// (pre-formatted "a: b; c: d"): rewrites the existing style value, or creates
    /// the whole style attribute when absent. Single patch by construction, so a
    /// batch of style edits can never mint duplicate style attributes.
    bool ComputeStyleDeclarationPatch(int node, const std::string& declarations, RmlPatch& out) const;
    /// Removal patch for one specific attribute instance; duplicate attribute
    /// names (invalid but historically saveable) are legal input.
    bool ComputeAttributeRemovalPatch(const RmlAttribute& attribute, RmlPatch& out) const;
    bool ComputeTextPatch(int node, const std::string& newText, RmlPatch& out) const;
    bool ComputeElementRemovalPatch(int node, RmlPatch& out) const;
    bool ComputeInsertPatch(int parent, int childOrdinal, const std::string& markup, RmlPatch& out) const;
    // Sort by descending offset, splice every replacement into the buffer, then re-parse once.
    void ApplyPatches(const std::vector<RmlPatch>& patches);

    // ---- Standard-RML generators (no proprietary syntax). Style/quote/indent follow the file. ----
    // Build an element open tag: <tag id=".." class=".." style="..">; extraAttrs are raw "name=\"value\"".
    std::string MakeElement(const std::string& tag, const std::string& id, const std::string& className,
        const std::string& style, bool selfClose, int styleSampleNode = -1) const;

private:
    std::string text_;
    std::vector<RmlNode> nodes_;

    std::string detectedIndent_ = "  "; // leading whitespace of the first indented child line
    char detectedQuote_ = '"';

    void Parse();
    void DetectStyle();
    // Parse "a: b; c: d" over [valueOffset, valueOffset+valueLength) of text_ into out.
    void ParseStyleDeclarations(int valueOffset, int valueLength, std::vector<RmlStyleDecl>& out) const;
    std::string markupTagOf(const std::string& markup) const;

    // Replace a byte range then re-parse the whole buffer (documents are small; re-parse keeps
    // every span consistent with near-zero state-tracking risk).
    void ApplyPatch(const RmlSpan& span, const std::string& replacement);
};
